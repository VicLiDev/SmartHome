# Makefile — Xiaomi MIoT Gateway (Rockchip 嵌入式)
#
# 编译目标:
#   make              — 本机 x86_64 测试
#   make ARCH=arm     — ARM 32位 (RK3288/RK3399)
#   make ARCH=aarch64 — ARM 64位 (RK3588/RK3576/RK3568)
#
# 用法:
#   make              # 编译
#   make run          # 运行扫描
#   make clean        # 清理
#   make install      # 安装到系统

CC       ?= gcc
CFLAGS   := -Wall -Wextra -std=c11 -D_GNU_SOURCE
LDFLAGS  := -lssl -lcrypto -lpthread

# 交叉编译工具链
ifeq ($(ARCH),arm)
CC  := arm-linux-gnueabihf-gcc
endif
ifeq ($(ARCH),aarch64)
CC  := aarch64-linux-gnu-gcc
endif

# 目录
SRC_DIR   := src
INC_DIR   := inc
OBJ_DIR   := build
THIRD_DIR := third_party/cJSON
TARGET    := bin/miio_gateway

# 源文件
SRCS := $(wildcard $(SRC_DIR)/*.c)
SRCS += $(THIRD_DIR)/cJSON.c

OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(filter $(SRC_DIR)/%.c,$(SRCS)))
OBJS += $(patsubst $(THIRD_DIR)/%.c,$(OBJ_DIR)/%.o,$(filter $(THIRD_DIR)/%.c,$(SRCS)))

CFLAGS += -I$(INC_DIR) -I$(THIRD_DIR)

ifdef DEBUG
CFLAGS += -g -O0 -DDEBUG
else
CFLAGS += -O2
endif

.PHONY: all clean install run help

all: info $(TARGET)
	@echo ""
	@echo "✓ 编译完成: $(TARGET)"

info:
	@echo "━━━ Xiaomi Gateway Build ━━━"
	@echo "  CC:      $(CC)"
	@echo "  ARCH:    $(ARCH:host)"
	@echo "  Sources: $(words $(OBJS)) files"
	@echo "  Target:  $(TARGET)"
	@echo ""

$(TARGET): $(OBJS) | bin
	@echo "链接: $@"
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	@echo "编译: $< → $@"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: $(THIRD_DIR)/%.c | $(OBJ_DIR)
	@echo "编译(3rd): $< → $@"
	$(CC) $(CFLAGS) -w -c $< -o $@

$(OBJ_DIR) bin:
	mkdir -p $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
	@echo "✓ 清理完成"

run: $(TARGET)
	./$(TARGET) scan --timeout 5

install: $(TARGET)
	install -d /usr/local/bin /etc/miio
	install -m 755 $(TARGET) /usr/local/bin/
	@[ -f config/gateway.conf ] && install -m 644 config/gateway.conf /etc/miio/ || true
	@echo "✓ 已安装"

help:
	@echo "可用目标:"
	@echo "  make              # 编译（本机）"
	@echo "  make ARCH=aarch64 # 交叉编译 ARM64"
	@echo "  make DEBUG=1      # 调试模式"
	@echo "  make run           # 扫描测试"
	@echo "  make install       # 安装到系统"
	@echo "  make clean         # 清理"
