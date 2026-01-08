#ifndef EFFECT_CLIENT_H
#define EFFECT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Effect handle returned by EffectClient_Open
 */
typedef void* EffectHandle;

/**
 * Effect type enumeration
 */
typedef enum {
    EFFECT_TYPE_KARAOKE_NO_MIC = 0,
    EFFECT_TYPE_NOISE_REDUCTION = 1,
} EffectType;

/**
 * Audio configuration
 */
typedef struct {
    uint32_t sampleRate;      // Sample rate in Hz (e.g., 48000)
    uint32_t channels;        // Number of channels (1, 2, etc.)
    uint32_t format;          // Audio format (16=PCM_16, 32=PCM_32, etc.)
    uint32_t framesPerBuffer; // Frames per processing callback
} EffectConfig;

/**
 * Result codes
 */
typedef enum {
    EFFECT_OK = 0,
    EFFECT_ERROR_INVALID_ARGUMENTS = -1,
    EFFECT_ERROR_NO_MEMORY = -2,
    EFFECT_ERROR_INVALID_STATE = -3,
    EFFECT_ERROR_NOT_SUPPORTED = -4,
    EFFECT_ERROR_TIMEOUT = -5,
    EFFECT_ERROR_DEAD_OBJECT = -6,
} EffectResult;

/**
 * Effect statistics
 */
typedef struct {
    uint64_t processedFrames;
    uint64_t droppedFrames;
    uint32_t avgLatencyUs;
    uint32_t p95LatencyUs;
    uint32_t maxLatencyUs;
    uint32_t timeoutCount;
    uint32_t xrunCount;
} EffectStats;

/**
 * Open a new effect session
 * 
 * This function connects to the effectd service and creates a new session.
 * It must be called from a non-real-time thread.
 * 
 * @param effectType Type of effect (karaoke, noise reduction, etc.)
 * @param config Audio configuration
 * @param handle Output parameter for effect handle (valid if return is EFFECT_OK)
 * @return EFFECT_OK on success, error code otherwise
 */
EffectResult EffectClient_Open(EffectType effectType, const EffectConfig* config, EffectHandle* handle);

/**
 * Start processing for a session
 * 
 * Must be called from a non-real-time thread.
 * 
 * @param handle Effect handle returned by EffectClient_Open
 * @return EFFECT_OK on success, error code otherwise
 */
EffectResult EffectClient_Start(EffectHandle handle);

/**
 * Process audio data (real-time safe)
 * 
 * This function can be called from the HAL real-time thread.
 * It performs only lock-free ring buffer operations and eventfd signaling.
 * No HIDL calls, no dynamic memory allocation, no heavy locks.
 * 
 * If processing times out (>20ms), the function returns EFFECT_ERROR_TIMEOUT
 * and the HAL should fall back to passthrough mode.
 * 
 * @param handle Effect handle
 * @param input Input PCM buffer
 * @param output Output PCM buffer (can be same as input for in-place)
 * @param frames Number of frames to process
 * @return EFFECT_OK on success, EFFECT_ERROR_TIMEOUT on timeout, error code otherwise
 */
EffectResult EffectClient_Process(EffectHandle handle, const void* input, void* output, uint32_t frames);

/**
 * Set algorithm parameter
 * 
 * Must be called from a non-real-time thread.
 * 
 * @param handle Effect handle
 * @param key Parameter key
 * @param value Parameter value buffer
 * @param valueSize Size of value buffer
 * @return EFFECT_OK on success, error code otherwise
 */
EffectResult EffectClient_SetParam(EffectHandle handle, uint32_t key, const void* value, uint32_t valueSize);

/**
 * Query statistics
 * 
 * Can be called from any thread.
 * 
 * @param handle Effect handle
 * @param stats Output parameter for statistics
 * @return EFFECT_OK on success, error code otherwise
 */
EffectResult EffectClient_QueryStats(EffectHandle handle, EffectStats* stats);

/**
 * Stop processing
 * 
 * Must be called from a non-real-time thread.
 * 
 * @param handle Effect handle
 * @return EFFECT_OK on success, error code otherwise
 */
EffectResult EffectClient_Stop(EffectHandle handle);

/**
 * Close and release session
 * 
 * Must be called from a non-real-time thread.
 * 
 * @param handle Effect handle
 * @return EFFECT_OK on success, error code otherwise
 */
EffectResult EffectClient_Close(EffectHandle handle);

#ifdef __cplusplus
}
#endif

#endif // EFFECT_CLIENT_H
