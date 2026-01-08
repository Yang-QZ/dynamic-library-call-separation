#include "effect_ringbuffer.h"
#include <string.h>
#include <stdatomic.h>

void effect_ringbuffer_init(effect_ringbuffer_t* rb, void* data, uint32_t capacity) {
    atomic_init(&rb->write_index, 0);
    atomic_init(&rb->read_index, 0);
    rb->capacity = capacity;
    rb->data = (uint8_t*)data;
}

uint32_t effect_ringbuffer_get_read_available(const effect_ringbuffer_t* rb) {
    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);
    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_acquire);
    return (uint32_t)(write_idx - read_idx);
}

uint32_t effect_ringbuffer_get_write_available(const effect_ringbuffer_t* rb) {
    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);
    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_acquire);
    uint32_t used = (uint32_t)(write_idx - read_idx);
    return rb->capacity - used;
}

uint32_t effect_ringbuffer_write(effect_ringbuffer_t* rb, const void* data, uint32_t size) {
    if (size == 0) return 0;
    
    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_relaxed);
    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_acquire);
    
    uint32_t available = rb->capacity - (uint32_t)(write_idx - read_idx);
    uint32_t to_write = (size < available) ? size : available;
    
    if (to_write == 0) return 0;
    
    // Calculate position in circular buffer
    uint32_t pos = (uint32_t)(write_idx % rb->capacity);
    uint32_t contiguous = rb->capacity - pos;
    
    if (to_write <= contiguous) {
        // Single contiguous write
        memcpy(rb->data + pos, data, to_write);
    } else {
        // Split write (wrap around)
        memcpy(rb->data + pos, data, contiguous);
        memcpy(rb->data, (const uint8_t*)data + contiguous, to_write - contiguous);
    }
    
    // Update write index with release semantics
    atomic_store_explicit(&rb->write_index, write_idx + to_write, memory_order_release);
    
    return to_write;
}

uint32_t effect_ringbuffer_read(effect_ringbuffer_t* rb, void* data, uint32_t size) {
    if (size == 0) return 0;
    
    uint64_t write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);
    uint64_t read_idx = atomic_load_explicit(&rb->read_index, memory_order_relaxed);
    
    uint32_t available = (uint32_t)(write_idx - read_idx);
    uint32_t to_read = (size < available) ? size : available;
    
    if (to_read == 0) return 0;
    
    // Calculate position in circular buffer
    uint32_t pos = (uint32_t)(read_idx % rb->capacity);
    uint32_t contiguous = rb->capacity - pos;
    
    if (to_read <= contiguous) {
        // Single contiguous read
        memcpy(data, rb->data + pos, to_read);
    } else {
        // Split read (wrap around)
        memcpy(data, rb->data + pos, contiguous);
        memcpy((uint8_t*)data + contiguous, rb->data, to_read - contiguous);
    }
    
    // Update read index with release semantics
    atomic_store_explicit(&rb->read_index, read_idx + to_read, memory_order_release);
    
    return to_read;
}

void effect_ringbuffer_reset(effect_ringbuffer_t* rb) {
    atomic_store_explicit(&rb->write_index, 0, memory_order_release);
    atomic_store_explicit(&rb->read_index, 0, memory_order_release);
}
