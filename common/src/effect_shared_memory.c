#include "effect_shared_memory.h"
#include <unistd.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <sys/syscall.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>

// Try memfd_create first, fallback to ashmem
#ifndef __NR_memfd_create
#if defined(__x86_64__)
#define __NR_memfd_create 319
#elif defined(__i386__)
#define __NR_memfd_create 356
#elif defined(__aarch64__)
#define __NR_memfd_create 279
#elif defined(__arm__)
#define __NR_memfd_create 385
#else
#define __NR_memfd_create 319  // Default fallback
#endif
#endif

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

static int memfd_create_wrapper(const char* name, unsigned int flags) {
    return (int)syscall(__NR_memfd_create, name, flags);
}

int effect_shared_memory_create(const char* name, size_t size) {
    int fd = -1;
    
    // Try memfd_create first (Linux 3.17+)
    fd = memfd_create_wrapper(name, MFD_CLOEXEC);
    
    if (fd < 0) {
        // Fallback to ashmem for older Android
#ifdef __ANDROID__
        fd = open("/dev/ashmem", O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "effect-%s", name);
            // ioctl(fd, ASHMEM_SET_NAME, buf);
            // ioctl(fd, ASHMEM_SET_SIZE, size);
        }
#else
        // On non-Android, try shm_open
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name); // Unlink immediately so it's cleaned up
        }
#endif
    }
    
    if (fd < 0) {
        return -1;
    }
    
    // Set the size
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    
    return fd;
}

void* effect_shared_memory_map(int fd, size_t size) {
    void* addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        return NULL;
    }
    return addr;
}

int effect_shared_memory_unmap(void* addr, size_t size) {
    return munmap(addr, size);
}

int effect_eventfd_create(unsigned int initval) {
    return eventfd(initval, EFD_CLOEXEC | EFD_NONBLOCK);
}

int effect_eventfd_signal(int fd) {
    uint64_t val = 1;
    ssize_t ret = write(fd, &val, sizeof(val));
    return (ret == sizeof(val)) ? 0 : -1;
}

int effect_eventfd_wait(int fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    int ret = poll(&pfd, 1, timeout_ms);
    if (ret <= 0) {
        return -1; // Timeout or error
    }
    
    // Read the eventfd to clear it
    uint64_t val;
    ssize_t read_ret = read(fd, &val, sizeof(val));
    return (read_ret == sizeof(val)) ? 0 : -1;
}
