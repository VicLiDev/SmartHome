# mijia_scanner_c — 米家设备网络探测器 (C 语言版)
# 对齐 Python 版本: mijia_scanner/

CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -O2 -D_GNU_SOURCE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=1 -Wno-stringop-truncation -Wno-format-truncation
LDFLAGS  = -lpthread

# 自动检测 libcurl (需要头文件 + 链接库)
HAS_CURL := $(shell (test -f /usr/include/curl/curl.h || test -f /usr/include/x86_64-linux-gnu/curl/curl.h) && echo yes || echo no)
ifeq ($(HAS_CURL),yes)
    CFLAGS  += -DHAVE_LIBCURL
    LDFLAGS += -lcurl
endif

# 源文件
SRCDIR   = src
SRCS     = $(SRCDIR)/main.c \
           $(SRCDIR)/color.c \
           $(SRCDIR)/device_db.c \
           $(SRCDIR)/protocol.c \
           $(SRCDIR)/network.c \
           $(SRCDIR)/mdns.c \
           $(SRCDIR)/ha.c \
           $(SRCDIR)/output.c

OBJDIR   = obj
OBJS     = $(SRCS:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

TARGET   = mijia_scanner

# ─── 目标 ───────────────────────────────────────────────

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  编译成功: ./$(TARGET)"
ifeq ($(HAS_CURL),yes)
	@echo "  [OK] libcurl 已启用 (HA API 支持)"
else
	@echo "  [--] libcurl 未检测到 (HA API 功能不可用)"
	@echo "       安装: sudo apt install libcurl4-openssl-dev && make clean && make"
endif
	@echo "  用法: ./$(TARGET) [--help]"
	@echo ""

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

# ─── 依赖（common.h 改动时全部重编）──────────────────────

$(OBJS): $(SRCDIR)/common.h
