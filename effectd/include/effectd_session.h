#ifndef EFFECTD_SESSION_H
#define EFFECTD_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

// Forward declare for C compatibility
#ifdef __cplusplus
extern "C" {
#endif

typedef void* EffectFmqHandle;

#ifdef __cplusplus
}
#endif

#include "effect_ringbuffer.h"

// Use FMQ by default on Android, fallback to shared memory on other platforms
#ifndef USE_SHARED_MEMORY
#define USE_FMQ 1
#else
#define USE_FMQ 0
#endif

typedef enum {
    SESSION_STATE_IDLE = 0,
    SESSION_STATE_OPENED = 1,
    SESSION_STATE_STARTED = 2,
    SESSION_STATE_STOPPED = 3,
    SESSION_STATE_ERROR = 4,
} SessionState;

typedef enum {
    EFFECT_LIB_KARAOKE_NO_MIC = 0,
    EFFECT_LIB_NOISE_REDUCTION = 1,
} EffectLibType;

typedef struct {
    uint32_t sampleRate;
    uint32_t channels;
    uint32_t format;
    uint32_t framesPerBuffer;
} AudioConfig;

typedef struct {
    uint64_t processedFrames;
    uint64_t droppedFrames;
    uint32_t avgLatencyUs;
    uint32_t p95LatencyUs;
    uint32_t maxLatencyUs;
    uint32_t timeoutCount;
    uint32_t xrunCount;
} SessionStats;

typedef struct EffectSession {
    uint32_t sessionId;
    EffectLibType effectType;
    AudioConfig config;
    SessionState state;
    
#if USE_FMQ
    // FMQ-based communication
    EffectFmqHandle inputFmq;
    EffectFmqHandle outputFmq;
#else
    // Shared memory (legacy)
    void* shmAddr;
    size_t shmSize;
    
    // Ring buffers
    effect_ringbuffer_t inputRb;
    effect_ringbuffer_t outputRb;
#endif
    
    // Event FDs
    int eventFdIn;   // HAL -> effectd
    int eventFdOut;  // effectd -> HAL
    
    // Third-party library handle
    void* libHandle;
    void* libContext;
    
    // Processing thread
    pthread_t processingThread;
    bool threadRunning;
    
    // Statistics
    SessionStats stats;
    pthread_mutex_t statsMutex;
    
} EffectSession;

/**
 * Create a new effect session
 */
EffectSession* effectd_session_create(uint32_t sessionId, EffectLibType effectType, 
                                      const AudioConfig* config);

/**
 * Open session and initialize third-party library
 */
int effectd_session_open(EffectSession* session);

/**
 * Start processing thread
 */
int effectd_session_start(EffectSession* session);

/**
 * Stop processing thread
 */
int effectd_session_stop(EffectSession* session);

/**
 * Close and destroy session
 */
void effectd_session_destroy(EffectSession* session);

/**
 * Set algorithm parameter
 */
int effectd_session_set_param(EffectSession* session, uint32_t key, 
                              const void* value, uint32_t valueSize);

/**
 * Query session state
 */
SessionState effectd_session_get_state(EffectSession* session);

/**
 * Query session statistics
 */
void effectd_session_get_stats(EffectSession* session, SessionStats* stats);

#endif // EFFECTD_SESSION_H
