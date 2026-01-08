# 快速集成指南 (Quick Integration Guide)

## 概述

本指南帮助您快速将 effectd 集成到现有的 Android Audio HAL 中。

## 前提条件

- Android 源码环境
- Audio HAL 代码访问权限
- Vendor 分区构建权限

## 集成步骤

### 步骤 1: 添加依赖

在 Audio HAL 的 `Android.bp` 中添加:

```bp
cc_library_shared {
    name: "android.hardware.audio@X.X-impl",
    // ... 其他配置 ...
    shared_libs: [
        // ... 其他依赖 ...
        "libeffect_client",
        "libeffect_common",
    ],
}
```

### 步骤 2: 包含头文件

在需要使用音效处理的源文件中:

```c
#include "effect_client.h"
```

### 步骤 3: 初始化音效会话

在 HAL 初始化阶段 (非实时线程):

```c
// 1. 定义配置
EffectConfig config = {
    .sampleRate = 48000,           // 采样率
    .channels = 2,                 // 声道数
    .format = 16,                  // PCM_16 位
    .framesPerBuffer = 480         // 每次处理的帧数
};

// 2. 打开会话
EffectHandle effectHandle;
EffectResult result = EffectClient_Open(
    EFFECT_TYPE_NOISE_REDUCTION,   // 或 EFFECT_TYPE_KARAOKE_NO_MIC
    &config,
    &effectHandle
);

if (result != EFFECT_OK) {
    ALOGE("Failed to open effect session: %d", result);
    // 降级为 passthrough 模式
    effectHandle = NULL;
}

// 3. 启动处理
if (effectHandle) {
    result = EffectClient_Start(effectHandle);
    if (result != EFFECT_OK) {
        ALOGE("Failed to start effect: %d", result);
        EffectClient_Close(effectHandle);
        effectHandle = NULL;
    }
}
```

### 步骤 4: 实时音频处理

在音频回调函数中 (实时线程安全):

```c
void audio_callback(void* context, int16_t* inputBuffer, 
                   int16_t* outputBuffer, uint32_t frames) {
    MyAudioContext* ctx = (MyAudioContext*)context;
    
    if (ctx->effectHandle) {
        // 使用音效处理
        EffectResult result = EffectClient_Process(
            ctx->effectHandle,
            inputBuffer,
            outputBuffer,
            frames
        );
        
        if (result == EFFECT_ERROR_TIMEOUT) {
            // 超时发生，output 已经是 passthrough 数据
            ALOGW("Effect processing timeout, using passthrough");
            ctx->timeoutCount++;
            
            // 如果超时过多，考虑禁用音效
            if (ctx->timeoutCount > 100) {
                ALOGE("Too many timeouts, disabling effect");
                EffectClient_Stop(ctx->effectHandle);
                EffectClient_Close(ctx->effectHandle);
                ctx->effectHandle = NULL;
            }
        } else if (result == EFFECT_OK) {
            // 处理成功
            if (ctx->timeoutCount > 0) {
                ctx->timeoutCount--;  // 逐渐恢复
            }
        } else {
            // 其他错误，使用 passthrough
            memcpy(outputBuffer, inputBuffer, frames * 2 * sizeof(int16_t));
        }
    } else {
        // 无音效或已禁用，直接 passthrough
        memcpy(outputBuffer, inputBuffer, frames * 2 * sizeof(int16_t));
    }
}
```

### 步骤 5: 参数设置 (可选)

在非实时线程中设置算法参数:

```c
void set_effect_parameter(EffectHandle handle, uint32_t key, void* value, uint32_t size) {
    if (handle) {
        EffectResult result = EffectClient_SetParam(handle, key, value, size);
        if (result != EFFECT_OK) {
            ALOGE("Failed to set parameter %u: %d", key, result);
        }
    }
}

// 使用示例
uint32_t noiseLevel = 3;
set_effect_parameter(effectHandle, PARAM_NOISE_LEVEL, &noiseLevel, sizeof(noiseLevel));
```

### 步骤 6: 查询统计信息

定期查询性能统计 (非实时线程):

```c
void print_effect_stats(EffectHandle handle) {
    if (!handle) return;
    
    EffectStats stats;
    EffectResult result = EffectClient_QueryStats(handle, &stats);
    
    if (result == EFFECT_OK) {
        ALOGI("Effect Statistics:");
        ALOGI("  Processed frames: %llu", stats.processedFrames);
        ALOGI("  Dropped frames:   %llu", stats.droppedFrames);
        ALOGI("  Avg latency:      %u us", stats.avgLatencyUs);
        ALOGI("  P95 latency:      %u us", stats.p95LatencyUs);
        ALOGI("  Max latency:      %u us", stats.maxLatencyUs);
        ALOGI("  Timeout count:    %u", stats.timeoutCount);
        ALOGI("  Xrun count:       %u", stats.xrunCount);
        
        // 检查是否超过目标延迟
        if (stats.p95LatencyUs > 10000) {
            ALOGW("P95 latency exceeds 10ms target!");
        }
    }
}
```

### 步骤 7: 清理资源

在 HAL 关闭时 (非实时线程):

```c
void cleanup_effect(EffectHandle handle) {
    if (handle) {
        // 打印最终统计
        print_effect_stats(handle);
        
        // 停止并关闭
        EffectClient_Stop(handle);
        EffectClient_Close(handle);
        
        ALOGI("Effect session closed");
    }
}
```

## 完整示例

参见 `examples/hal_integration.c` 获取完整的可运行示例。

## 系统配置

### 1. 添加 effectd 到构建

在 `device.mk` 中:

```makefile
PRODUCT_PACKAGES += \
    effectd \
    libeffect_client \
    libeffect_common
```

### 2. 配置 SELinux

确保 `sepolicy/` 中的策略文件被包含到您的设备构建中。

### 3. 验证服务启动

```bash
# 检查 effectd 是否运行
adb shell ps -A | grep effectd

# 查看日志
adb logcat | grep effectd

# 手动启动 (调试用)
adb shell start effectd
```

## 调试技巧

### 查看共享内存

```bash
# 查看进程的共享内存映射
adb shell cat /proc/$(pidof effectd)/maps | grep /dev

# 查看文件描述符
adb shell ls -l /proc/$(pidof effectd)/fd
```

### 性能分析

```bash
# CPU 使用率
adb shell top | grep effectd

# 线程优先级
adb shell ps -T | grep effectd

# 延迟追踪 (需要 systrace)
adb shell atrace --async_start audio
# ... 运行测试 ...
adb shell atrace --async_stop > trace.html
```

### 崩溃测试

```bash
# 模拟 effectd 崩溃
adb shell killall -SEGV effectd

# 观察自动重启
adb logcat | grep effectd

# 验证 HAL 是否继续运行
# 播放音频应该继续但无音效处理
```

## 常见问题

### Q: 如何知道 effectd 是否可用?

A: `EffectClient_Open()` 会在无法连接时返回错误，此时应降级为 passthrough。

### Q: 延迟太高怎么办?

A: 
1. 检查 effectd 的 CPU 优先级和亲和性
2. 增加 ring buffer 大小
3. 使用 systrace 分析调度问题
4. 检查是否有 CPU 频率调节问题

### Q: 如何支持新的音效类型?

A:
1. 在 `hidl/1.0/types.hal` 中添加新的 `EffectType`
2. 在 effectd 中添加对应的第三方库加载逻辑
3. 更新客户端 API 文档

### Q: 多个 HAL 实例如何共享 effectd?

A: effectd 支持多个客户端和多个 session，每个 HAL 实例都可以独立创建自己的 session。

## 性能优化建议

1. **Ring Buffer 大小**: 根据延迟要求调整，默认 1MB
2. **实时优先级**: 确保 effectd 和 HAL 都有正确的实时优先级
3. **CPU 亲和性**: 考虑将 effectd 固定到特定 CPU 核心
4. **内存对齐**: 确保 PCM buffer 是缓存行对齐的
5. **避免抖动**: 使用 `SCHED_FIFO` 并避免频率调节

## 支持

遇到问题请:
1. 查看日志: `adb logcat | grep -E "(effectd|effect_client)"`
2. 检查统计: 调用 `EffectClient_QueryStats()`
3. 参考文档: `DESIGN.md` 和 `IMPLEMENTATION.md`
4. 提交 Issue: GitHub 仓库

## 更多资源

- **设计文档**: [DESIGN.md](DESIGN.md)
- **实现总结**: [IMPLEMENTATION.md](IMPLEMENTATION.md)
- **示例代码**: [examples/hal_integration.c](examples/hal_integration.c)
- **单元测试**: [tests/unit/test_ringbuffer.c](tests/unit/test_ringbuffer.c)
