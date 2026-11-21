# control_core — portable build for generic Linux (default) and QNX SDP 8.0.
#
# `make` builds the POSIX backend (src/ipc_posix.c) with the host cc/gcc and is
# the target used for Linux tests and latency measurements.
#
# `make qnx` cross-compiles with qcc against QNX SDP 8.0, swapping
# src/ipc_posix.c for src/ipc_qnx.c (native MsgSend/MsgReceive/MsgReply).
# Nothing else in the tree changes: both backends implement the same
# include/ipc_transport.h contract. Select the architecture with
# QNX_ARCH=aarch64le (default) or QNX_ARCH=x86_64, and have qnxsdp-env.sh
# sourced first. See docs/qnx_port_plan.md.

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
LDLIBS  := -lm

INC     := -Iinclude -Isrc

BUILD   := build
BIN     := $(BUILD)/control_core
BENCH   := $(BUILD)/bench_client
TEST_TELEM  := $(BUILD)/test_telemetry_parse
TEST_LAYOUT := $(BUILD)/test_protocol_layout
TEST_URSCRIPT := $(BUILD)/test_urscript

CORE_SRCS := src/ipc_posix.c src/robot_link.c src/latency_log.c \
             src/clock_util.c src/urscript.c src/main.c
CORE_OBJS := $(CORE_SRCS:src/%.c=$(BUILD)/%.o)

LIB_SRCS  := src/ipc_posix.c src/robot_link.c src/latency_log.c \
             src/clock_util.c src/urscript.c
LIB_OBJS  := $(LIB_SRCS:src/%.c=$(BUILD)/%.o)

# ---- QNX SDP 8.0 cross-build (needs qcc + qnxsdp-env.sh sourced) ----------
QNX_ARCH    ?= aarch64le
QNX_CC      ?= qcc
QNX_CFLAGS  ?= -Vgcc_nto$(QNX_ARCH) -std=c11 -Wall -Wextra -O2 -g
QNX_LDLIBS  ?= -lm -lsocket

QNX_BUILD   := $(BUILD)/qnx
QNX_BIN     := $(QNX_BUILD)/control_core.qnx
QNX_BENCH   := $(QNX_BUILD)/bench_client.qnx

QNX_CORE_SRCS := $(subst src/ipc_posix.c,src/ipc_qnx.c,$(CORE_SRCS))
QNX_CORE_OBJS := $(QNX_CORE_SRCS:src/%.c=$(QNX_BUILD)/%.o)

QNX_LIB_SRCS := $(subst src/ipc_posix.c,src/ipc_qnx.c,$(LIB_SRCS))
QNX_LIB_OBJS := $(QNX_LIB_SRCS:src/%.c=$(QNX_BUILD)/%.o)

.PHONY: all clean test qnx sanitize analyze

all: $(BIN) $(BENCH)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) -c $< -o $@

$(BIN): $(CORE_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BENCH): tools/bench_client.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDLIBS)

$(TEST_TELEM): tests/test_telemetry_parse.c $(LIB_OBJS)
	$(CC) $(CFLAGS) $(INC) $^ -o $@ $(LDLIBS)

$(TEST_LAYOUT): tests/test_protocol_layout.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< -o $@ $(LDLIBS)

$(TEST_URSCRIPT): tests/test_urscript.c build/urscript.o | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< build/urscript.o -o $@ $(LDLIBS)

test: $(TEST_TELEM) $(TEST_LAYOUT) $(TEST_URSCRIPT)
	./$(TEST_TELEM)
	./$(TEST_LAYOUT)
	./$(TEST_URSCRIPT)
	cd python_client && python3 -c "import control_client; print('OK: control_client.py struct layout asserts passed')"

# ---- QNX ------------------------------------------------------------------
qnx: $(QNX_BIN) $(QNX_BENCH)

$(QNX_BUILD):
	mkdir -p $(QNX_BUILD)

$(QNX_BUILD)/%.o: src/%.c | $(QNX_BUILD)
	$(QNX_CC) $(QNX_CFLAGS) $(INC) -c $< -o $@

$(QNX_BIN): $(QNX_CORE_OBJS)
	$(QNX_CC) $(QNX_CFLAGS) $^ -o $@ $(QNX_LDLIBS)

$(QNX_BENCH): tools/bench_client.c $(QNX_LIB_OBJS)
	$(QNX_CC) $(QNX_CFLAGS) $(INC) $^ -o $@ $(QNX_LDLIBS)

# ---- Hardening targets -----------------------------------------------------
SANITIZE_CFLAGS := -std=c11 -Wall -Wextra -O1 -g \
                   -fsanitize=address,undefined -fno-omit-frame-pointer
SANITIZE_LDLIBS := -lm -fsanitize=address,undefined

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(SANITIZE_CFLAGS)" LDLIBS="$(SANITIZE_LDLIBS)" test

analyze:
	$(CC) -std=c11 -Wall -Wextra -fanalyzer $(INC) -c src/main.c -o $(BUILD)/main.analyzer.o
	$(CC) -std=c11 -Wall -Wextra -fanalyzer $(INC) -c src/robot_link.c -o $(BUILD)/robot_link.analyzer.o
	$(CC) -std=c11 -Wall -Wextra -fanalyzer $(INC) -c src/urscript.c -o $(BUILD)/urscript.analyzer.o

clean:
	rm -rf $(BUILD)
