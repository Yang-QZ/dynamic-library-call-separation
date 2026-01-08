#include "effect_client.h"
#include "effect_shared_memory.h"
#include "effect_ringbuffer.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define MAX_BUFFER_SIZE (1024 * 1024)  // 1MB for ring buffers
#define TIMEOUT_MS 20

typedef struct {
    uint32_t sessionId;
    EffectType effectType;
    EffectConfig config;
    
    // Shared memory
    int shmFd;
    void* shmAddr;
    size_t shmSize;
    
    // Event FDs
    int eventFdIn;   // HAL -> effectd
    int eventFdOut;  // effectd -> HAL
    
    // Ring buffers
    effect_ringbuffer_t inputRb;
    effect_ringbuffer_t outputRb;
    
    // Statistics
    EffectStats stats;
    pthread_mutex_t statsMutex;
    
    // State
    bool isStarted;
    bool isConnected;
    
} EffectSession;

static uint32_t calculate_bytes_per_frame(const EffectConfig* config) {
    uint32_t bytes_per_sample = (config->format == 16) ? 2 : 4;
    return config->channels * bytes_per_sample;
}

static int64_t get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

EffectResult EffectClient_Open(EffectType effectType, const EffectConfig* config, EffectHandle* handle) {
    if (!config || !handle) {
        return EFFECT_ERROR_INVALID_ARGUMENTS;
    }
    
    // Allocate session
    EffectSession* session = (EffectSession*)calloc(1, sizeof(EffectSession));
    if (!session) {
        return EFFECT_ERROR_NO_MEMORY;
    }
    
    session->effectType = effectType;
    session->config = *config;
    session->sessionId = (uint32_t)getpid(); // Simple session ID
    
    pthread_mutex_init(&session->statsMutex, NULL);
    
    // Create shared memory for ring buffers
    size_t ringBufferSize = MAX_BUFFER_SIZE;
    session->shmSize = ringBufferSize * 2; // Input + output
    
    session->shmFd = effect_shared_memory_create("effect_shm", session->shmSize);
    if (session->shmFd < 0) {
        free(session);
        return EFFECT_ERROR_NO_MEMORY;
    }
    
    session->shmAddr = effect_shared_memory_map(session->shmFd, session->shmSize);
    if (!session->shmAddr) {
        close(session->shmFd);
        free(session);
        return EFFECT_ERROR_NO_MEMORY;
    }
    
    // Initialize ring buffers
    effect_ringbuffer_init(&session->inputRb, session->shmAddr, ringBufferSize);
    effect_ringbuffer_init(&session->outputRb, 
                          (uint8_t*)session->shmAddr + ringBufferSize, 
                          ringBufferSize);
    
    // Create event FDs
    session->eventFdIn = effect_eventfd_create(0);
    session->eventFdOut = effect_eventfd_create(0);
    
    if (session->eventFdIn < 0 || session->eventFdOut < 0) {
        if (session->eventFdIn >= 0) close(session->eventFdIn);
        if (session->eventFdOut >= 0) close(session->eventFdOut);
        effect_shared_memory_unmap(session->shmAddr, session->shmSize);
        close(session->shmFd);
        free(session);
        return EFFECT_ERROR_NO_MEMORY;
    }
    
    session->isConnected = true;
    
    // TODO: In real implementation, connect to effectd via HIDL here
    // and pass the FDs to the service
    
    *handle = (EffectHandle)session;
    return EFFECT_OK;
}

EffectResult EffectClient_Start(EffectHandle handle) {
    if (!handle) {
        return EFFECT_ERROR_INVALID_ARGUMENTS;
    }
    
    EffectSession* session = (EffectSession*)handle;
    
    if (!session->isConnected) {
        return EFFECT_ERROR_DEAD_OBJECT;
    }
    
    session->isStarted = true;
    
    // TODO: Call HIDL start() method
    
    return EFFECT_OK;
}

EffectResult EffectClient_Process(EffectHandle handle, const void* input, void* output, uint32_t frames) {
    if (!handle || !input || !output || frames == 0) {
        return EFFECT_ERROR_INVALID_ARGUMENTS;
    }
    
    EffectSession* session = (EffectSession*)handle;
    
    if (!session->isStarted) {
        return EFFECT_ERROR_INVALID_STATE;
    }
    
    int64_t start_time = get_time_us();
    
    uint32_t bytesPerFrame = calculate_bytes_per_frame(&session->config);
    uint32_t totalBytes = frames * bytesPerFrame;
    
    // Write input to ring buffer
    uint32_t written = effect_ringbuffer_write(&session->inputRb, input, totalBytes);
    if (written < totalBytes) {
        // Buffer full - this is an underrun
        pthread_mutex_lock(&session->statsMutex);
        session->stats.xrunCount++;
        pthread_mutex_unlock(&session->statsMutex);
        return EFFECT_ERROR_TIMEOUT;
    }
    
    // Signal effectd that data is available
    effect_eventfd_signal(session->eventFdIn);
    
    // Wait for output data with timeout
    int wait_result = effect_eventfd_wait(session->eventFdOut, TIMEOUT_MS);
    if (wait_result < 0) {
        pthread_mutex_lock(&session->statsMutex);
        session->stats.timeoutCount++;
        pthread_mutex_unlock(&session->statsMutex);
        
        // Timeout - fall back to passthrough
        memcpy(output, input, totalBytes);
        return EFFECT_ERROR_TIMEOUT;
    }
    
    // Read output from ring buffer
    uint32_t read = effect_ringbuffer_read(&session->outputRb, output, totalBytes);
    if (read < totalBytes) {
        // Not enough data - passthrough
        memcpy(output, input, totalBytes);
        
        pthread_mutex_lock(&session->statsMutex);
        session->stats.droppedFrames += frames;
        pthread_mutex_unlock(&session->statsMutex);
        
        return EFFECT_ERROR_TIMEOUT;
    }
    
    // Update statistics
    int64_t end_time = get_time_us();
    uint32_t latency = (uint32_t)(end_time - start_time);
    
    pthread_mutex_lock(&session->statsMutex);
    session->stats.processedFrames += frames;
    
    // Simple rolling average for latency
    if (session->stats.avgLatencyUs == 0) {
        session->stats.avgLatencyUs = latency;
    } else {
        session->stats.avgLatencyUs = (session->stats.avgLatencyUs * 9 + latency) / 10;
    }
    
    if (latency > session->stats.maxLatencyUs) {
        session->stats.maxLatencyUs = latency;
    }
    
    // Approximate P95 (simplified)
    if (latency > session->stats.p95LatencyUs) {
        session->stats.p95LatencyUs = latency;
    }
    
    pthread_mutex_unlock(&session->statsMutex);
    
    return EFFECT_OK;
}

EffectResult EffectClient_SetParam(EffectHandle handle, 
                                   uint32_t key __attribute__((unused)), 
                                   const void* value, uint32_t valueSize) {
    if (!handle || !value || valueSize == 0) {
        return EFFECT_ERROR_INVALID_ARGUMENTS;
    }
    
    EffectSession* session = (EffectSession*)handle;
    
    if (!session->isConnected) {
        return EFFECT_ERROR_DEAD_OBJECT;
    }
    
    // TODO: Call HIDL setParam() method
    
    return EFFECT_OK;
}

EffectResult EffectClient_QueryStats(EffectHandle handle, EffectStats* stats) {
    if (!handle || !stats) {
        return EFFECT_ERROR_INVALID_ARGUMENTS;
    }
    
    EffectSession* session = (EffectSession*)handle;
    
    pthread_mutex_lock(&session->statsMutex);
    *stats = session->stats;
    pthread_mutex_unlock(&session->statsMutex);
    
    return EFFECT_OK;
}

EffectResult EffectClient_Stop(EffectHandle handle) {
    if (!handle) {
        return EFFECT_ERROR_INVALID_ARGUMENTS;
    }
    
    EffectSession* session = (EffectSession*)handle;
    
    session->isStarted = false;
    
    // TODO: Call HIDL stop() method
    
    return EFFECT_OK;
}

EffectResult EffectClient_Close(EffectHandle handle) {
    if (!handle) {
        return EFFECT_ERROR_INVALID_ARGUMENTS;
    }
    
    EffectSession* session = (EffectSession*)handle;
    
    // TODO: Call HIDL close() method
    
    // Clean up
    if (session->eventFdIn >= 0) close(session->eventFdIn);
    if (session->eventFdOut >= 0) close(session->eventFdOut);
    
    if (session->shmAddr) {
        effect_shared_memory_unmap(session->shmAddr, session->shmSize);
    }
    
    if (session->shmFd >= 0) {
        close(session->shmFd);
    }
    
    pthread_mutex_destroy(&session->statsMutex);
    
    free(session);
    
    return EFFECT_OK;
}
