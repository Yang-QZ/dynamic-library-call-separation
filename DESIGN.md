# Audio Effect Process Separation (effectd)

## Overview

This project implements crash isolation for third-party audio algorithms in Android Audio HAL by moving them to an independent process (`effectd`). This prevents crashes in third-party libraries from bringing down the entire Audio HAL process.

## Architecture

### Components

1. **effectd** - Independent vendor process that loads and executes third-party audio processing libraries
2. **libeffect_client.so** - Client library for Audio HAL to communicate with effectd
3. **libeffect_common.a** - Shared utilities (ring buffer, shared memory management)
4. **HIDL Interface** - Control plane communication (vendor.audio.effectservice@1.0)

### Communication Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Audio HAL                                │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  libeffect_client.so                                       │ │
│  │  - Process(): Lock-free ringbuffer read/write             │ │
│  │  - 20ms timeout with passthrough fallback                 │ │
│  └────────────────────────────────────────────────────────────┘ │
└───────────────────┬────────────────────────────┬─────────────────┘
                    │                            │
          Control Plane (HIDL)          Data Plane (Shared Memory)
     open/start/stop/close/setParam      PCM via lock-free ringbuffer
                    │                            │
┌───────────────────┴────────────────────────────┴─────────────────┐
│                         effectd Process                           │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Session Manager                                           │ │
│  │  - Multi-session support                                   │ │
│  │  - Per-session processing thread                           │ │
│  │  - SCHED_FIFO with audio priority                          │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Third-party Library Loader (dlopen/dlsym)                │ │
│  │  - libwt_ksong_signalprocessing.so (no-mic karaoke)      │ │
│  │  - libwt_signalprocessing.so (noise reduction)           │ │
│  └────────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────┘
```

## Key Features

### 1. Crash Isolation
- Third-party library crashes (SIGSEGV, etc.) only affect effectd process
- Audio HAL continues running
- init automatically restarts effectd
- Client library detects disconnect and enters passthrough mode

### 2. Low Latency Data Path
- **Target**: Additional latency < 10ms
- Lock-free ring buffer implementation using atomic operations
- Shared memory (memfd/ashmem) for zero-copy data transfer
- eventfd for efficient signaling (no syscalls in common case)
- Real-time thread safe: no HIDL calls, no malloc, no heavy locks

### 3. Multi-Instance Support
- Concurrent sessions (e.g., karaoke + noise reduction)
- Independent shared memory region per session
- Independent eventfd pair per session
- Per-session processing thread with isolation

### 4. Timeout and Fallback
- 20ms timeout on Process() calls
- Automatic passthrough on timeout
- Statistics tracking (timeouts, xruns, latency P50/P95/Max)

## Directory Structure

```
.
├── hidl/1.0/                   # HIDL interface definitions
│   ├── IEffectService.hal      # Service interface
│   └── types.hal               # Data types
├── common/                     # Shared utilities
│   ├── include/
│   │   ├── effect_shared_memory.h
│   │   └── effect_ringbuffer.h
│   └── src/
│       ├── effect_shared_memory.c
│       └── effect_ringbuffer.c
├── client/                     # HAL-side client library
│   ├── include/
│   │   └── effect_client.h     # Public API for HAL
│   └── src/
│       └── effect_client.c     # Implementation
├── effectd/                    # Server process
│   ├── include/
│   │   └── effectd_session.h
│   └── src/
│       ├── main.c              # Entry point
│       └── effectd_session.c   # Session management
├── sepolicy/                   # SELinux policies
│   ├── effectd.te
│   ├── file_contexts
│   └── service_contexts
├── tests/                      # Tests
│   └── unit/
│       └── test_ringbuffer.c
├── Android.bp                  # Android build configuration
├── Makefile                    # Standalone build
├── effectd.rc                  # init service definition
└── README.md                   # This file
```

## API Usage

### HAL Integration Example

```c
#include "effect_client.h"

// Open session (non-RT thread)
EffectConfig config = {
    .sampleRate = 48000,
    .channels = 2,
    .format = 16,  // PCM_16
    .framesPerBuffer = 480
};

EffectHandle handle;
EffectResult result = EffectClient_Open(
    EFFECT_TYPE_NOISE_REDUCTION,
    &config,
    &handle
);

if (result != EFFECT_OK) {
    // Handle error - fallback to passthrough
}

// Start processing (non-RT thread)
EffectClient_Start(handle);

// Process audio (RT thread - safe to call)
// This function never blocks more than 20ms
void audio_callback(void* input, void* output, uint32_t frames) {
    result = EffectClient_Process(handle, input, output, frames);
    
    if (result == EFFECT_ERROR_TIMEOUT) {
        // Timeout occurred - output already contains passthrough
        // Consider logging and monitoring
    }
}

// Query statistics (any thread)
EffectStats stats;
EffectClient_QueryStats(handle, &stats);
printf("Avg latency: %u us, Timeouts: %u\n", 
       stats.avgLatencyUs, stats.timeoutCount);

// Stop and close (non-RT thread)
EffectClient_Stop(handle);
EffectClient_Close(handle);
```

## Building

### Android Build System

```bash
# In Android source tree
mmm vendor/audio/effect-separation
# Or add to PRODUCT_PACKAGES in device.mk:
PRODUCT_PACKAGES += effectd libeffect_client
```

### Standalone Build (for testing)

```bash
make clean
make
make test
```

## Testing

### Unit Tests

```bash
make test
# Or directly:
./test_ringbuffer
```

### Integration Tests

1. **Crash Isolation Test**: Kill effectd process while audio is playing
   ```bash
   adb shell killall -SEGV effectd
   # Audio should continue (passthrough)
   # effectd should restart automatically
   ```

2. **Latency Test**: Monitor statistics during playback
   ```bash
   adb shell dumpsys audio.effectd stats
   ```

3. **Multi-Instance Test**: Run karaoke and noise reduction simultaneously

## Performance Targets

| Metric | Target | Measurement |
|--------|--------|-------------|
| Additional Latency P50 | < 5ms | Per-session stats |
| Additional Latency P95 | < 10ms | Per-session stats |
| Timeout Rate | < 0.1% | Timeout count / total frames |
| CPU Overhead | < 5% | System profiling |

## Configuration

### Real-time Priority (effectd.rc)

```
priority -20
ioprio rt 4
```

### CPU Affinity

Modify effectd.rc to pin to specific cores:
```
writepid /dev/cpuset/audio-app/tasks
```

### Ring Buffer Size

Adjust in code based on latency requirements:
```c
#define MAX_BUFFER_SIZE (1024 * 1024)  // 1MB default
```

## Troubleshooting

### High Timeout Rate
- Increase ring buffer size
- Check effectd CPU priority and affinity
- Verify no CPU frequency scaling issues

### Memory Issues
- Check for leaks with valgrind or AddressSanitizer
- Monitor with `adb shell dumpsys meminfo effectd`

### SELinux Denials
```bash
adb shell dmesg | grep avc | grep effectd
# Add missing permissions to sepolicy/effectd.te
```

## Future Work

### AIDL Migration Path

The current HIDL interface (vendor.audio.effectservice@1.0) is designed to be easily migrated to AIDL:

1. Define AIDL interface paralleling current HIDL
2. Implement AIDL service alongside HIDL
3. Update client library to support both
4. Migrate HAL integration to AIDL
5. Deprecate HIDL interface

### Additional Features

- [ ] Dynamic session configuration
- [ ] Hot-reload of third-party libraries
- [ ] Per-session CPU affinity control
- [ ] Enhanced statistics (histograms)
- [ ] Memory usage optimization
- [ ] Power consumption monitoring

## License

See LICENSE file for details.

## Contact

For issues and questions, please open a GitHub issue.
