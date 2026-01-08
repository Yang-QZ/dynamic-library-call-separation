#ifndef EFFECT_RINGBUFFER_H
#define EFFECT_RINGBUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Lock-free ring buffer for PCM data transfer
 * Uses atomic operations for thread-safe single-producer single-consumer
 */
typedef struct {
    atomic_uint_fast64_t write_index;  // Write position (producer)
    atomic_uint_fast64_t read_index;   // Read position (consumer)
    uint32_t capacity;                 // Buffer capacity in bytes
    uint8_t* data;                     // Data buffer
} effect_ringbuffer_t;

/**
 * Initialize ring buffer
 * 
 * @param rb Ring buffer structure
 * @param data Data buffer (must be allocated by caller)
 * @param capacity Capacity in bytes (should be power of 2 for best performance)
 */
void effect_ringbuffer_init(effect_ringbuffer_t* rb, void* data, uint32_t capacity);

/**
 * Get available bytes for reading
 * 
 * @param rb Ring buffer
 * @return Number of bytes available
 */
uint32_t effect_ringbuffer_get_read_available(const effect_ringbuffer_t* rb);

/**
 * Get available space for writing
 * 
 * @param rb Ring buffer
 * @return Number of bytes available
 */
uint32_t effect_ringbuffer_get_write_available(const effect_ringbuffer_t* rb);

/**
 * Write data to ring buffer (non-blocking)
 * 
 * @param rb Ring buffer
 * @param data Source data
 * @param size Number of bytes to write
 * @return Number of bytes actually written
 */
uint32_t effect_ringbuffer_write(effect_ringbuffer_t* rb, const void* data, uint32_t size);

/**
 * Read data from ring buffer (non-blocking)
 * 
 * @param rb Ring buffer
 * @param data Destination buffer
 * @param size Number of bytes to read
 * @return Number of bytes actually read
 */
uint32_t effect_ringbuffer_read(effect_ringbuffer_t* rb, void* data, uint32_t size);

/**
 * Reset ring buffer (clear all data)
 * NOTE: Only safe to call when no concurrent operations
 * 
 * @param rb Ring buffer
 */
void effect_ringbuffer_reset(effect_ringbuffer_t* rb);

#ifdef __cplusplus
}
#endif

#endif // EFFECT_RINGBUFFER_H
