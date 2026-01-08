#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "effect_ringbuffer.h"

#define TEST_BUFFER_SIZE 1024

void test_ringbuffer_basic() {
    printf("Running test_ringbuffer_basic...\n");
    
    uint8_t buffer[TEST_BUFFER_SIZE];
    effect_ringbuffer_t rb;
    
    effect_ringbuffer_init(&rb, buffer, TEST_BUFFER_SIZE);
    
    // Check initial state
    assert(effect_ringbuffer_get_read_available(&rb) == 0);
    assert(effect_ringbuffer_get_write_available(&rb) == TEST_BUFFER_SIZE);
    
    // Write some data
    uint8_t write_data[256];
    for (int i = 0; i < 256; i++) {
        write_data[i] = (uint8_t)i;
    }
    
    uint32_t written = effect_ringbuffer_write(&rb, write_data, 256);
    assert(written == 256);
    assert(effect_ringbuffer_get_read_available(&rb) == 256);
    assert(effect_ringbuffer_get_write_available(&rb) == TEST_BUFFER_SIZE - 256);
    
    // Read some data
    uint8_t read_data[256];
    uint32_t read = effect_ringbuffer_read(&rb, read_data, 256);
    assert(read == 256);
    assert(memcmp(write_data, read_data, 256) == 0);
    assert(effect_ringbuffer_get_read_available(&rb) == 0);
    
    printf("✓ test_ringbuffer_basic passed\n");
}

void test_ringbuffer_wrap_around() {
    printf("Running test_ringbuffer_wrap_around...\n");
    
    uint8_t buffer[256];
    effect_ringbuffer_t rb;
    
    effect_ringbuffer_init(&rb, buffer, 256);
    
    // Fill buffer
    uint8_t data[256];
    for (int i = 0; i < 256; i++) {
        data[i] = (uint8_t)i;
    }
    
    // Write 200 bytes
    effect_ringbuffer_write(&rb, data, 200);
    
    // Read 150 bytes
    uint8_t temp[200];
    effect_ringbuffer_read(&rb, temp, 150);
    
    // Now write 200 more bytes (should wrap around)
    effect_ringbuffer_write(&rb, data, 200);
    
    // Read all data
    uint8_t read_data[250];
    uint32_t read = effect_ringbuffer_read(&rb, read_data, 250);
    assert(read == 250);
    
    // Verify first 50 bytes (remaining from first write)
    assert(memcmp(read_data, data + 150, 50) == 0);
    // Verify next 200 bytes (from second write)
    assert(memcmp(read_data + 50, data, 200) == 0);
    
    printf("✓ test_ringbuffer_wrap_around passed\n");
}

void test_ringbuffer_full() {
    printf("Running test_ringbuffer_full...\n");
    
    uint8_t buffer[256];
    effect_ringbuffer_t rb;
    
    effect_ringbuffer_init(&rb, buffer, 256);
    
    uint8_t data[512];
    for (int i = 0; i < 512; i++) {
        data[i] = (uint8_t)i;
    }
    
    // Try to write more than capacity
    uint32_t written = effect_ringbuffer_write(&rb, data, 512);
    assert(written == 256);  // Only 256 bytes should be written
    
    // Buffer should be full
    assert(effect_ringbuffer_get_write_available(&rb) == 0);
    assert(effect_ringbuffer_get_read_available(&rb) == 256);
    
    printf("✓ test_ringbuffer_full passed\n");
}

void test_ringbuffer_empty() {
    printf("Running test_ringbuffer_empty...\n");
    
    uint8_t buffer[256];
    effect_ringbuffer_t rb;
    
    effect_ringbuffer_init(&rb, buffer, 256);
    
    uint8_t read_data[128];
    
    // Try to read from empty buffer
    uint32_t read = effect_ringbuffer_read(&rb, read_data, 128);
    assert(read == 0);
    
    printf("✓ test_ringbuffer_empty passed\n");
}

void test_ringbuffer_reset() {
    printf("Running test_ringbuffer_reset...\n");
    
    uint8_t buffer[256];
    effect_ringbuffer_t rb;
    
    effect_ringbuffer_init(&rb, buffer, 256);
    
    uint8_t data[128];
    effect_ringbuffer_write(&rb, data, 128);
    
    assert(effect_ringbuffer_get_read_available(&rb) == 128);
    
    effect_ringbuffer_reset(&rb);
    
    assert(effect_ringbuffer_get_read_available(&rb) == 0);
    assert(effect_ringbuffer_get_write_available(&rb) == 256);
    
    printf("✓ test_ringbuffer_reset passed\n");
}

int main() {
    printf("Starting ring buffer tests...\n\n");
    
    test_ringbuffer_basic();
    test_ringbuffer_wrap_around();
    test_ringbuffer_full();
    test_ringbuffer_empty();
    test_ringbuffer_reset();
    
    printf("\n✓ All tests passed!\n");
    return 0;
}
