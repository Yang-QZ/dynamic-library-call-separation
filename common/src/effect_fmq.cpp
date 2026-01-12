#include "effect_fmq.h"

#ifdef __ANDROID__
// Android FMQ implementation using HIDL MessageQueue
#include <fmq/MessageQueue.h>
#include <fmq/EventFlag.h>
#include <hidl/MQDescriptor.h>

using android::hardware::MessageQueue;
using android::hardware::MQDescriptorSync;
using android::hardware::kSynchronizedReadWrite;

// Internal FMQ context structure
struct EffectFmqContext {
    MessageQueue<uint8_t, kSynchronizedReadWrite>* queue;
    EffectFmqType type;
    size_t elementSize;
};

EffectFmqHandle effect_fmq_create(EffectFmqType type, size_t capacity, size_t elementSize) {
    if (type != EFFECT_FMQ_SYNCHRONIZED) {
        // Only synchronized mode supported for now
        return nullptr;
    }
    
    auto* ctx = new EffectFmqContext();
    if (!ctx) {
        return nullptr;
    }
    
    ctx->type = type;
    ctx->elementSize = elementSize;
    
    // Create FMQ with specified capacity
    ctx->queue = new MessageQueue<uint8_t, kSynchronizedReadWrite>(capacity * elementSize);
    
    if (!ctx->queue || !ctx->queue->isValid()) {
        delete ctx->queue;
        delete ctx;
        return nullptr;
    }
    
    return (EffectFmqHandle)ctx;
}

EffectFmqHandle effect_fmq_open(const EffectFmqDescriptor* desc) {
    if (!desc) {
        return nullptr;
    }
    
    auto* ctx = new EffectFmqContext();
    if (!ctx) {
        return nullptr;
    }
    
    ctx->type = EFFECT_FMQ_SYNCHRONIZED;
    ctx->elementSize = 1; // byte-based
    
    // Reconstruct MQDescriptor from our descriptor
    std::vector<android::hardware::GrantorDescriptor> grantors;
    // Note: This is a simplified version. Full implementation would need to properly
    // reconstruct the MQDescriptor from the file descriptor and metadata
    
    // For now, we'll indicate this needs proper implementation
    // In real code, you'd pass the actual MQDescriptorSync from HIDL
    delete ctx;
    return nullptr; // TODO: Implement descriptor-based reconstruction
}

int effect_fmq_get_descriptor(EffectFmqHandle handle, EffectFmqDescriptor* desc) {
    if (!handle || !desc) {
        return -1;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    if (!ctx->queue) {
        return -1;
    }
    
    // Note: This is a simplified version. In actual HIDL usage, the descriptor
    // is passed directly as MQDescriptorSync<uint8_t> through HIDL interface
    // The descriptor contains native handles that can't be easily serialized to plain C
    
    // For compatibility, we're just zeroing the descriptor
    // In real implementation, this would be handled by HIDL serialization
    memset(desc, 0, sizeof(EffectFmqDescriptor));
    
    return 0; // TODO: Proper descriptor extraction
}

size_t effect_fmq_write(EffectFmqHandle handle, const void* data, size_t count) {
    if (!handle || !data || count == 0) {
        return 0;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    if (!ctx->queue) {
        return 0;
    }
    
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    return ctx->queue->write(bytes, count) ? count : 0;
}

ssize_t effect_fmq_write_blocking(EffectFmqHandle handle, const void* data, 
                                   size_t count, int timeoutMs) {
    if (!handle || !data || count == 0) {
        return -1;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    if (!ctx->queue) {
        return -1;
    }
    
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    
    // FMQ doesn't have built-in timeout for write, so we'll use non-blocking write
    // with a simple retry loop
    if (timeoutMs == 0) {
        return ctx->queue->write(bytes, count) ? count : -1;
    }
    
    // For blocking with timeout, we'd need to use EventFlag
    // For now, just do a non-blocking write
    return ctx->queue->write(bytes, count) ? count : -1;
}

size_t effect_fmq_read(EffectFmqHandle handle, void* data, size_t count) {
    if (!handle || !data || count == 0) {
        return 0;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    if (!ctx->queue) {
        return 0;
    }
    
    uint8_t* bytes = static_cast<uint8_t*>(data);
    return ctx->queue->read(bytes, count) ? count : 0;
}

ssize_t effect_fmq_read_blocking(EffectFmqHandle handle, void* data, 
                                  size_t count, int timeoutMs) {
    if (!handle || !data || count == 0) {
        return -1;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    if (!ctx->queue) {
        return -1;
    }
    
    uint8_t* bytes = static_cast<uint8_t*>(data);
    
    if (timeoutMs == 0) {
        return ctx->queue->read(bytes, count) ? count : -1;
    }
    
    // For blocking with timeout, we'd need to use EventFlag
    // For now, just do a non-blocking read
    return ctx->queue->read(bytes, count) ? count : -1;
}

size_t effect_fmq_available_to_write(EffectFmqHandle handle) {
    if (!handle) {
        return 0;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    if (!ctx->queue) {
        return 0;
    }
    
    return ctx->queue->availableToWrite();
}

size_t effect_fmq_available_to_read(EffectFmqHandle handle) {
    if (!handle) {
        return 0;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    if (!ctx->queue) {
        return 0;
    }
    
    return ctx->queue->availableToRead();
}

uint32_t* effect_fmq_get_event_flag_word(EffectFmqHandle handle) {
    if (!handle) {
        return nullptr;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    if (!ctx->queue) {
        return nullptr;
    }
    
    // EventFlag would need to be set up separately
    return nullptr;
}

void effect_fmq_destroy(EffectFmqHandle handle) {
    if (!handle) {
        return;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    
    if (ctx->queue) {
        delete ctx->queue;
    }
    
    delete ctx;
}

#else
// Non-Android fallback implementation using custom ring buffer
#include <stdlib.h>
#include <string.h>

// Forward declare C types
extern "C" {
#include "effect_ringbuffer.h"
}

struct EffectFmqContext {
    effect_ringbuffer_t ringbuffer;
    uint8_t* buffer;
    size_t capacity;
    EffectFmqType type;
    size_t elementSize;
};

EffectFmqHandle effect_fmq_create(EffectFmqType type, size_t capacity, size_t elementSize) {
    auto* ctx = new EffectFmqContext();
    if (!ctx) {
        return nullptr;
    }
    
    ctx->type = type;
    ctx->elementSize = elementSize;
    ctx->capacity = capacity * elementSize;
    
    ctx->buffer = new uint8_t[ctx->capacity];
    if (!ctx->buffer) {
        delete ctx;
        return nullptr;
    }
    
    effect_ringbuffer_init(&ctx->ringbuffer, ctx->buffer, ctx->capacity);
    
    return (EffectFmqHandle)ctx;
}

EffectFmqHandle effect_fmq_open(const EffectFmqDescriptor* desc) {
    // In fallback mode, cannot open from descriptor
    (void)desc;
    return nullptr;
}

int effect_fmq_get_descriptor(EffectFmqHandle handle, EffectFmqDescriptor* desc) {
    // In fallback mode, cannot get descriptor
    (void)handle;
    (void)desc;
    return -1;
}

size_t effect_fmq_write(EffectFmqHandle handle, const void* data, size_t count) {
    if (!handle || !data || count == 0) {
        return 0;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    return effect_ringbuffer_write(&ctx->ringbuffer, data, count);
}

ssize_t effect_fmq_write_blocking(EffectFmqHandle handle, const void* data, 
                                   size_t count, int timeoutMs) {
    (void)timeoutMs; // Fallback doesn't support blocking
    return effect_fmq_write(handle, data, count);
}

size_t effect_fmq_read(EffectFmqHandle handle, void* data, size_t count) {
    if (!handle || !data || count == 0) {
        return 0;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    return effect_ringbuffer_read(&ctx->ringbuffer, data, count);
}

ssize_t effect_fmq_read_blocking(EffectFmqHandle handle, void* data, 
                                  size_t count, int timeoutMs) {
    (void)timeoutMs; // Fallback doesn't support blocking
    return effect_fmq_read(handle, data, count);
}

size_t effect_fmq_available_to_write(EffectFmqHandle handle) {
    if (!handle) {
        return 0;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    return effect_ringbuffer_get_write_available(&ctx->ringbuffer);
}

size_t effect_fmq_available_to_read(EffectFmqHandle handle) {
    if (!handle) {
        return 0;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    return effect_ringbuffer_get_read_available(&ctx->ringbuffer);
}

uint32_t* effect_fmq_get_event_flag_word(EffectFmqHandle handle) {
    (void)handle;
    return nullptr;
}

void effect_fmq_destroy(EffectFmqHandle handle) {
    if (!handle) {
        return;
    }
    
    auto* ctx = static_cast<EffectFmqContext*>(handle);
    
    if (ctx->buffer) {
        delete[] ctx->buffer;
    }
    
    delete ctx;
}

#endif // __ANDROID__
