# Dynamic Library Call Separation for Android Audio HAL

适用于 Android Audio HAL，用于分离第三方动态库，实现崩溃隔离。

## 概述 (Overview)

本项目实现了将 Android Audio HAL 中的第三方音频算法迁移到独立进程 `effectd`，以实现崩溃隔离。当第三方库发生崩溃时，不会导致 Audio HAL 进程退出，从而保证系统音频链路的稳定性。

This project implements crash isolation for third-party audio algorithms in Android Audio HAL by moving them to an independent `effectd` process. When third-party libraries crash, the Audio HAL process remains stable.

## 关键特性 (Key Features)

- **崩溃隔离 (Crash Isolation)**: 三方库崩溃不影响 Audio HAL
- **低延迟 (Low Latency)**: 新增延迟 < 10ms
- **实时安全 (Real-time Safe)**: Process() 函数无 HIDL 调用、无动态内存分配、无重锁
- **多实例支持 (Multi-instance)**: 支持多个音效同时运行
- **超时降级 (Timeout Fallback)**: 20ms 超时自动 passthrough

## 快速开始 (Quick Start)

### 构建 (Build)

```bash
# Standalone build for testing
make

# Android build
mmm vendor/audio/effect-separation
```

### 测试 (Test)

```bash
# Run unit tests
make test

# Or directly
./test_ringbuffer
```

### 使用示例 (Usage Example)

参见 `examples/hal_integration.c` 获取完整的 HAL 集成示例。

See `examples/hal_integration.c` for complete HAL integration example.

```c
#include "effect_client.h"

// Open session
EffectConfig config = { .sampleRate = 48000, .channels = 2, .format = 16, .framesPerBuffer = 480 };
EffectHandle handle;
EffectClient_Open(EFFECT_TYPE_NOISE_REDUCTION, &config, &handle);

// Start processing
EffectClient_Start(handle);

// Process audio (real-time safe)
EffectClient_Process(handle, input, output, frames);

// Close
EffectClient_Stop(handle);
EffectClient_Close(handle);
```

## 架构 (Architecture)

详细设计文档请参见 [DESIGN.md](DESIGN.md)

For detailed design documentation, see [DESIGN.md](DESIGN.md)

```
Audio HAL (libeffect_client.so)
    ↕ HIDL (Control) + FMQ (Data)
effectd Process
    ↓ dlopen/dlsym
Third-party Libraries (libwt_ksong_signalprocessing.so, libwt_signalprocessing.so, etc.)
```

**数据传输 (Data Transfer)**: 
- **Android**: 使用 Fast Message Queue (FMQ) 进行零拷贝音频数据传输
- **Standalone**: 使用共享内存 + 自定义 ring buffer (兼容模式)

详见 [FMQ_MIGRATION.md](FMQ_MIGRATION.md) 了解 FMQ 实现细节。

## 目录结构 (Directory Structure)

- `hidl/` - HIDL 接口定义 (Interface definitions)
- `common/` - 共享工具库 (Shared utilities: ringbuffer, shared memory)
- `client/` - HAL 侧客户端库 (Client library for HAL)
- `effectd/` - 独立进程服务 (Server process)
- `sepolicy/` - SELinux 策略 (SELinux policies)
- `tests/` - 测试用例 (Test cases)
- `examples/` - 集成示例 (Integration examples)

## 性能指标 (Performance Metrics)

| 指标 (Metric) | 目标 (Target) |
|--------------|--------------|
| 额外延迟 P50 | < 5ms |
| 额外延迟 P95 | < 10ms |
| 超时率 | < 0.1% |
| CPU 开销 | < 5% |

## 文档 (Documentation)

- [DESIGN.md](DESIGN.md) - 详细设计文档 (Detailed design)
- [examples/hal_integration.c](examples/hal_integration.c) - HAL 集成示例 (Integration example)

## License

MIT License - See LICENSE file for details.
