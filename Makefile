# Makefile for building and testing outside Android build system

CC = gcc
CXX = g++
CFLAGS = -Wall -Wextra -std=c11 -pthread -D_GNU_SOURCE -DUSE_SHARED_MEMORY=1 -I./common/include -I./client/include -I./effectd/include
CXXFLAGS = -Wall -Wextra -std=c++11 -pthread -D_GNU_SOURCE -DUSE_SHARED_MEMORY=1 -I./common/include -I./client/include -I./effectd/include
LDFLAGS = -pthread -lrt -ldl

# Output binary names
SERVER_BIN = effectd_server
CLIENT_LIB = libeffect_client.so
COMMON_LIB = libeffect_common.a
TEST_BIN = test_ringbuffer

# Common library
COMMON_C_SRCS = common/src/effect_shared_memory.c common/src/effect_ringbuffer.c
COMMON_CPP_SRCS = common/src/effect_fmq.cpp
COMMON_C_OBJS = $(COMMON_C_SRCS:.c=.o)
COMMON_CPP_OBJS = $(COMMON_CPP_SRCS:.cpp=.o)
COMMON_OBJS = $(COMMON_C_OBJS) $(COMMON_CPP_OBJS)

# Client library
CLIENT_SRCS = client/src/effect_client.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)

# Server
SERVER_SRCS = effectd/src/main.c effectd/src/effectd_session.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)

# Test
TEST_SRCS = tests/unit/test_ringbuffer.c
TEST_OBJS = $(TEST_SRCS:.c=.o)

all: $(COMMON_LIB) $(CLIENT_LIB) $(SERVER_BIN) $(TEST_BIN)

$(COMMON_LIB): $(COMMON_OBJS)
	ar rcs $@ $^

$(CLIENT_LIB): $(CLIENT_OBJS) $(COMMON_LIB)
	$(CC) -shared -o $@ $^ $(LDFLAGS)

$(SERVER_BIN): $(SERVER_OBJS) $(COMMON_LIB)
	$(CXX) -o $@ $^ $(LDFLAGS)

$(TEST_BIN): $(TEST_OBJS) $(COMMON_LIB)
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

clean:
	rm -f $(COMMON_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) $(TEST_OBJS)
	rm -f $(COMMON_LIB) $(CLIENT_LIB) $(SERVER_BIN) $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

.PHONY: all clean test
