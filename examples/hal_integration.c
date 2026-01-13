/**
 * Example Audio HAL integration with effect client library
 * 
 * This demonstrates how to integrate libeffect_client.so into an Audio HAL
 * to process audio with crash isolation.
 * 
 * NOTE: The client library uses Android Fast Message Queue (FMQ) for data
 * transfer on Android platforms, and shared memory + ring buffer on standalone
 * builds. The API is identical regardless of implementation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "effect_client.h"

// Example audio HAL context
typedef struct {
    EffectHandle effectHandle;
    bool effectEnabled;
    bool passthroughMode;
    uint32_t timeoutCount;
} AudioHalContext;

/**
 * Initialize effect processing (called during HAL initialization)
 */
int hal_effect_init(AudioHalContext* ctx, uint32_t sampleRate, 
                   uint32_t channels, uint32_t framesPerBuffer) {
    EffectConfig config = {
        .sampleRate = sampleRate,
        .channels = channels,
        .format = 16,  // PCM_16
        .framesPerBuffer = framesPerBuffer
    };
    
    // Try to open noise reduction effect
    EffectResult result = EffectClient_Open(
        EFFECT_TYPE_NOISE_REDUCTION,
        &config,
        &ctx->effectHandle
    );
    
    if (result != EFFECT_OK) {
        fprintf(stderr, "Failed to open effect: %d, using passthrough\n", result);
        ctx->effectEnabled = false;
        ctx->passthroughMode = true;
        return -1;
    }
    
    // Start processing
    result = EffectClient_Start(ctx->effectHandle);
    if (result != EFFECT_OK) {
        fprintf(stderr, "Failed to start effect: %d\n", result);
        EffectClient_Close(ctx->effectHandle);
        ctx->effectEnabled = false;
        ctx->passthroughMode = true;
        return -1;
    }
    
    ctx->effectEnabled = true;
    ctx->passthroughMode = false;
    ctx->timeoutCount = 0;
    
    printf("Effect processing initialized successfully\n");
    return 0;
}

/**
 * Process audio in real-time thread (SAFE to call from RT context)
 * 
 * This function can be called from the HAL's real-time audio callback.
 * It will never block more than 20ms and will automatically fall back
 * to passthrough on timeout.
 */
void hal_effect_process(AudioHalContext* ctx, const int16_t* input, 
                       int16_t* output, uint32_t frames) {
    if (!ctx->effectEnabled || ctx->passthroughMode) {
        // Passthrough mode - just copy
        memcpy(output, input, frames * 2 * sizeof(int16_t)); // stereo
        return;
    }
    
    EffectResult result = EffectClient_Process(
        ctx->effectHandle,
        input,
        output,
        frames
    );
    
    if (result == EFFECT_ERROR_TIMEOUT) {
        // Timeout occurred - output already contains passthrough
        ctx->timeoutCount++;
        
        // If too many timeouts, disable effect
        if (ctx->timeoutCount > 100) {
            fprintf(stderr, "Too many timeouts (%u), disabling effect\n", 
                   ctx->timeoutCount);
            ctx->passthroughMode = true;
        }
    } else if (result == EFFECT_OK) {
        // Success - reset timeout counter
        if (ctx->timeoutCount > 0) {
            ctx->timeoutCount--;
        }
    } else {
        // Other error - use passthrough
        memcpy(output, input, frames * 2 * sizeof(int16_t));
    }
}

/**
 * Set effect parameter (called from non-RT thread)
 */
int hal_effect_set_param(AudioHalContext* ctx, uint32_t key, 
                        const void* value, uint32_t valueSize) {
    if (!ctx->effectEnabled) {
        return -1;
    }
    
    EffectResult result = EffectClient_SetParam(
        ctx->effectHandle,
        key,
        value,
        valueSize
    );
    
    return (result == EFFECT_OK) ? 0 : -1;
}

/**
 * Query effect statistics (called from non-RT thread)
 */
void hal_effect_print_stats(AudioHalContext* ctx) {
    if (!ctx->effectEnabled) {
        printf("Effect not enabled\n");
        return;
    }
    
    EffectStats stats;
    EffectResult result = EffectClient_QueryStats(ctx->effectHandle, &stats);
    
    if (result == EFFECT_OK) {
        printf("Effect Statistics:\n");
        printf("  Processed frames: %lu\n", stats.processedFrames);
        printf("  Dropped frames:   %lu\n", stats.droppedFrames);
        printf("  Avg latency:      %u us\n", stats.avgLatencyUs);
        printf("  P95 latency:      %u us\n", stats.p95LatencyUs);
        printf("  Max latency:      %u us\n", stats.maxLatencyUs);
        printf("  Timeout count:    %u\n", stats.timeoutCount);
        printf("  Xrun count:       %u\n", stats.xrunCount);
        
        if (stats.maxLatencyUs > 10000) {
            printf("  WARNING: Max latency exceeds 10ms target!\n");
        }
    }
}

/**
 * Cleanup (called during HAL shutdown)
 */
void hal_effect_cleanup(AudioHalContext* ctx) {
    if (!ctx->effectEnabled) {
        return;
    }
    
    printf("Shutting down effect processing...\n");
    
    // Print final statistics
    hal_effect_print_stats(ctx);
    
    // Stop and close
    EffectClient_Stop(ctx->effectHandle);
    EffectClient_Close(ctx->effectHandle);
    
    ctx->effectEnabled = false;
    
    printf("Effect processing shut down\n");
}

/**
 * Example main function demonstrating usage
 */
int main() {
    printf("Audio HAL Effect Integration Example\n\n");
    
    AudioHalContext ctx = {0};
    
    // Initialize effect
    if (hal_effect_init(&ctx, 48000, 2, 480) < 0) {
        printf("Running without effect processing\n");
    }
    
    // Simulate audio processing
    int16_t input[480 * 2];  // 480 frames, stereo
    int16_t output[480 * 2];
    
    // Fill input with test data
    for (int i = 0; i < 480 * 2; i++) {
        input[i] = (int16_t)(i % 1000);
    }
    
    printf("\nProcessing audio...\n");
    for (int i = 0; i < 100; i++) {
        hal_effect_process(&ctx, input, output, 480);
        
        // Print stats every 20 iterations
        if (i > 0 && i % 20 == 0) {
            printf("\nAfter %d iterations:\n", i);
            hal_effect_print_stats(&ctx);
        }
    }
    
    // Cleanup
    printf("\n");
    hal_effect_cleanup(&ctx);
    
    return 0;
}
