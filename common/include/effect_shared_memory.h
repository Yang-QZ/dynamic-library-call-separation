#ifndef EFFECT_SHARED_MEMORY_H
#define EFFECT_SHARED_MEMORY_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create shared memory using memfd_create (preferred) or ashmem (fallback)
 * 
 * @param name Name for the shared memory region
 * @param size Size in bytes
 * @return File descriptor on success, -1 on error
 */
int effect_shared_memory_create(const char* name, size_t size);

/**
 * Map shared memory to process address space
 * 
 * @param fd File descriptor from effect_shared_memory_create
 * @param size Size in bytes
 * @return Mapped address on success, NULL on error
 */
void* effect_shared_memory_map(int fd, size_t size);

/**
 * Unmap shared memory
 * 
 * @param addr Address returned by effect_shared_memory_map
 * @param size Size in bytes
 * @return 0 on success, -1 on error
 */
int effect_shared_memory_unmap(void* addr, size_t size);

/**
 * Create eventfd for signaling
 * 
 * @param initval Initial value
 * @return File descriptor on success, -1 on error
 */
int effect_eventfd_create(unsigned int initval);

/**
 * Signal eventfd (write 1)
 * 
 * @param fd Eventfd file descriptor
 * @return 0 on success, -1 on error
 */
int effect_eventfd_signal(int fd);

/**
 * Wait on eventfd with timeout
 * 
 * @param fd Eventfd file descriptor
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking, -1 for blocking)
 * @return 0 on success, -1 on error or timeout
 */
int effect_eventfd_wait(int fd, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif // EFFECT_SHARED_MEMORY_H
