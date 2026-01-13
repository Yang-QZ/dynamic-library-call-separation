#ifndef EFFECT_FMQ_H
#define EFFECT_FMQ_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Opaque handle for FMQ (Fast Message Queue)
 */
typedef void* EffectFmqHandle;

/**
 * FMQ type enumeration
 */
typedef enum {
    EFFECT_FMQ_SYNCHRONIZED,    // Single reader, single writer with blocking
    EFFECT_FMQ_UNSYNCHRONIZED   // Single writer, multiple readers
} EffectFmqType;

/**
 * FMQ descriptor structure for passing between processes
 * This wraps the HIDL MQDescriptor
 */
typedef struct {
    int sharedMemoryFd;
    uint64_t size;
    uint32_t quantum;
    uint32_t flags;
    // Additional fields needed for FMQ descriptor
    uint64_t readPtr;
    uint64_t writePtr;
} EffectFmqDescriptor;

/**
 * Create a new FMQ (producer/writer side)
 * 
 * @param type FMQ type (synchronized or unsynchronized)
 * @param capacity Number of elements the queue can hold
 * @param elementSize Size of each element in bytes
 * @return FMQ handle on success, NULL on error
 */
EffectFmqHandle effect_fmq_create(EffectFmqType type, size_t capacity, size_t elementSize);

/**
 * Open an FMQ from descriptor (consumer/reader side)
 * 
 * @param desc FMQ descriptor received from creator
 * @return FMQ handle on success, NULL on error
 */
EffectFmqHandle effect_fmq_open(const EffectFmqDescriptor* desc);

/**
 * Get FMQ descriptor for passing to other process
 * 
 * @param handle FMQ handle
 * @param desc Output parameter for descriptor
 * @return 0 on success, -1 on error
 */
int effect_fmq_get_descriptor(EffectFmqHandle handle, EffectFmqDescriptor* desc);

/**
 * Write data to FMQ (non-blocking)
 * 
 * @param handle FMQ handle
 * @param data Source data buffer
 * @param count Number of bytes to write
 * @return Number of bytes actually written
 */
size_t effect_fmq_write(EffectFmqHandle handle, const void* data, size_t count);

/**
 * Write data to FMQ with timeout
 * 
 * @param handle FMQ handle
 * @param data Source data buffer
 * @param count Number of bytes to write
 * @param timeoutMs Timeout in milliseconds (0 for non-blocking)
 * @return Number of bytes actually written, -1 on timeout
 */
ssize_t effect_fmq_write_blocking(EffectFmqHandle handle, const void* data, 
                                   size_t count, int timeoutMs);

/**
 * Read data from FMQ (non-blocking)
 * 
 * @param handle FMQ handle
 * @param data Destination buffer
 * @param count Number of bytes to read
 * @return Number of bytes actually read
 */
size_t effect_fmq_read(EffectFmqHandle handle, void* data, size_t count);

/**
 * Read data from FMQ with timeout
 * 
 * @param handle FMQ handle
 * @param data Destination buffer
 * @param count Number of bytes to read
 * @param timeoutMs Timeout in milliseconds (0 for non-blocking)
 * @return Number of bytes actually read, -1 on timeout
 */
ssize_t effect_fmq_read_blocking(EffectFmqHandle handle, void* data, 
                                  size_t count, int timeoutMs);

/**
 * Get available space for writing
 * 
 * @param handle FMQ handle
 * @return Number of bytes available for writing
 */
size_t effect_fmq_available_to_write(EffectFmqHandle handle);

/**
 * Get available data for reading
 * 
 * @param handle FMQ handle
 * @return Number of bytes available for reading
 */
size_t effect_fmq_available_to_read(EffectFmqHandle handle);

/**
 * Get event flag word for synchronization
 * 
 * @param handle FMQ handle
 * @return Event flag word address, NULL if not available
 */
uint32_t* effect_fmq_get_event_flag_word(EffectFmqHandle handle);

/**
 * Close and destroy FMQ
 * 
 * @param handle FMQ handle
 */
void effect_fmq_destroy(EffectFmqHandle handle);

#ifdef __cplusplus
}
#endif

#endif // EFFECT_FMQ_H
