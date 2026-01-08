#include "effectd_session.h"
#include "effect_shared_memory.h"
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>

#define MAX_BUFFER_SIZE (1024 * 1024)

static uint32_t calculate_bytes_per_frame(const AudioConfig* config) {
    uint32_t bytes_per_sample = (config->format == 16) ? 2 : 4;
    return config->channels * bytes_per_sample;
}

static int64_t get_time_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

static void mock_process_audio(void* context __attribute__((unused)), 
                               const void* input, void* output, 
                               uint32_t frames, uint32_t bytesPerFrame) {
    // Simple passthrough for now
    // Real implementation would call the loaded library's process function
    memcpy(output, input, frames * bytesPerFrame);
    
    // Simulate some processing time (1-2ms)
    usleep(1000 + (rand() % 1000));
}

static void* processing_thread_func(void* arg) {
    EffectSession* session = (EffectSession*)arg;
    uint32_t bytesPerFrame = calculate_bytes_per_frame(&session->config);
    uint32_t bufferSize = session->config.framesPerBuffer * bytesPerFrame;
    
    // Allocate processing buffers
    void* inputBuffer = malloc(bufferSize);
    void* outputBuffer = malloc(bufferSize);
    
    if (!inputBuffer || !outputBuffer) {
        free(inputBuffer);
        free(outputBuffer);
        return NULL;
    }
    
    // Try to set real-time priority
    struct sched_param param;
    param.sched_priority = 10; // Medium priority
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
    
    while (session->threadRunning) {
        // Wait for input data notification
        int wait_result = effect_eventfd_wait(session->eventFdIn, 100); // 100ms timeout
        
        if (wait_result < 0) {
            // Timeout or error - continue waiting
            continue;
        }
        
        int64_t start_time = get_time_us();
        
        // Read input from ring buffer
        uint32_t available = effect_ringbuffer_get_read_available(&session->inputRb);
        if (available < bufferSize) {
            // Not enough data
            continue;
        }
        
        uint32_t read = effect_ringbuffer_read(&session->inputRb, inputBuffer, bufferSize);
        if (read < bufferSize) {
            pthread_mutex_lock(&session->statsMutex);
            session->stats.xrunCount++;
            pthread_mutex_unlock(&session->statsMutex);
            continue;
        }
        
        // Process audio with third-party library
        mock_process_audio(session->libContext, inputBuffer, outputBuffer, 
                          session->config.framesPerBuffer, bytesPerFrame);
        
        // Write output to ring buffer
        uint32_t written = effect_ringbuffer_write(&session->outputRb, outputBuffer, bufferSize);
        if (written < bufferSize) {
            pthread_mutex_lock(&session->statsMutex);
            session->stats.droppedFrames += session->config.framesPerBuffer;
            pthread_mutex_unlock(&session->statsMutex);
            continue;
        }
        
        // Signal output data available
        effect_eventfd_signal(session->eventFdOut);
        
        // Update statistics
        int64_t end_time = get_time_us();
        uint32_t latency = (uint32_t)(end_time - start_time);
        
        pthread_mutex_lock(&session->statsMutex);
        session->stats.processedFrames += session->config.framesPerBuffer;
        
        if (session->stats.avgLatencyUs == 0) {
            session->stats.avgLatencyUs = latency;
        } else {
            session->stats.avgLatencyUs = (session->stats.avgLatencyUs * 9 + latency) / 10;
        }
        
        if (latency > session->stats.maxLatencyUs) {
            session->stats.maxLatencyUs = latency;
        }
        
        if (latency > session->stats.p95LatencyUs) {
            session->stats.p95LatencyUs = latency;
        }
        
        pthread_mutex_unlock(&session->statsMutex);
    }
    
    free(inputBuffer);
    free(outputBuffer);
    
    return NULL;
}

EffectSession* effectd_session_create(uint32_t sessionId, EffectLibType effectType, 
                                      const AudioConfig* config) {
    EffectSession* session = (EffectSession*)calloc(1, sizeof(EffectSession));
    if (!session) {
        return NULL;
    }
    
    session->sessionId = sessionId;
    session->effectType = effectType;
    session->config = *config;
    session->state = SESSION_STATE_IDLE;
    session->eventFdIn = -1;
    session->eventFdOut = -1;
    
    pthread_mutex_init(&session->statsMutex, NULL);
    
    return session;
}

int effectd_session_open(EffectSession* session) {
    if (!session || session->state != SESSION_STATE_IDLE) {
        return -1;
    }
    
    // Load third-party library
    // In real implementation, would load library with dlopen
    // const char* libPath = NULL;
    // switch (session->effectType) {
    //     case EFFECT_LIB_KARAOKE_NO_MIC:
    //         libPath = "libkaraoke.so";
    //         break;
    //     case EFFECT_LIB_NOISE_REDUCTION:
    //         libPath = "libnoise_reduction.so";
    //         break;
    //     default:
    //         return -1;
    // }
    
    // session->libHandle = dlopen(libPath, RTLD_NOW);
    // if (!session->libHandle) {
    //     return -1;
    // }
    
    // Initialize library context
    // session->libContext = ...;
    
    session->state = SESSION_STATE_OPENED;
    return 0;
}

int effectd_session_start(EffectSession* session) {
    if (!session || session->state != SESSION_STATE_OPENED) {
        return -1;
    }
    
    session->threadRunning = true;
    
    if (pthread_create(&session->processingThread, NULL, processing_thread_func, session) != 0) {
        session->threadRunning = false;
        return -1;
    }
    
    session->state = SESSION_STATE_STARTED;
    return 0;
}

int effectd_session_stop(EffectSession* session) {
    if (!session || session->state != SESSION_STATE_STARTED) {
        return -1;
    }
    
    session->threadRunning = false;
    pthread_join(session->processingThread, NULL);
    
    session->state = SESSION_STATE_STOPPED;
    return 0;
}

void effectd_session_destroy(EffectSession* session) {
    if (!session) {
        return;
    }
    
    if (session->state == SESSION_STATE_STARTED) {
        effectd_session_stop(session);
    }
    
    // Unload library
    if (session->libHandle) {
        dlclose(session->libHandle);
    }
    
    // Clean up event FDs (if owned by session)
    // Note: In real implementation, FDs are passed from client
    
    pthread_mutex_destroy(&session->statsMutex);
    
    free(session);
}

int effectd_session_set_param(EffectSession* session, 
                              uint32_t key __attribute__((unused)), 
                              const void* value __attribute__((unused)), 
                              uint32_t valueSize __attribute__((unused))) {
    if (!session) {
        return -1;
    }
    
    // TODO: Call third-party library's setParam function
    
    return 0;
}

SessionState effectd_session_get_state(EffectSession* session) {
    if (!session) {
        return SESSION_STATE_ERROR;
    }
    return session->state;
}

void effectd_session_get_stats(EffectSession* session, SessionStats* stats) {
    if (!session || !stats) {
        return;
    }
    
    pthread_mutex_lock(&session->statsMutex);
    *stats = session->stats;
    pthread_mutex_unlock(&session->statsMutex);
}
