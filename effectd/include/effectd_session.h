#ifndef EFFECTD_SESSION_H
#define EFFECTD_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "effect_ringbuffer.h"

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
    
    // Shared memory
    void* shmAddr;
    size_t shmSize;
    
    // Event FDs
    int eventFdIn;   // HAL -> effectd
    int eventFdOut;  // effectd -> HAL
    
    // Ring buffers
    effect_ringbuffer_t inputRb;
    effect_ringbuffer_t outputRb;
    
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
