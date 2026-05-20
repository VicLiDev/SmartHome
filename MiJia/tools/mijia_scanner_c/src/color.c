/**
 * color.c — ANSI 彩色终端输出
 *
 * 对齐 Python 版本: color.py
 * 提供 display_width(), pad_cjk(), Color 类
 * 零外部依赖，仅依赖 isatty()。
 *
 * 重要: 所有返回 const char* 的函数使用独立静态缓冲区，
 *       避免同一 printf 中多次调用互相覆盖。
 */

#include "common.h"
#include <stdarg.h>

int g_no_color = 0;

static bool _color_is_tty = false;

void color_init(void) {
    _color_is_tty = isatty(STDOUT_FILENO);
}

/**
 * display_width — 计算字符串在终端的显示宽度（中文/全角=2，ASCII=1）
 * 对齐 Python color.py: display_width()
 */
int display_width(const char *text) {
    if (!text) return 0;
    int w = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t cp = 0;
        int bytes = 0;

        if (*p < 0x80) {
            cp = *p; bytes = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F; bytes = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F; bytes = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07; bytes = 4;
        } else {
            p++; w++; continue;
        }

        for (int i = 1; i < bytes && p[i]; i++)
            cp = (cp << 6) | (p[i] & 0x3F);

        p += bytes;

        /* CJK / 全角检测 — 对齐 Python 代码的码点范围 */
        if (cp >= 0x1100 && (
            (cp <= 0x115F) ||              /* Hangul Jamo */
            (0x2329 <= cp && cp <= 0x232A) ||
            (0x2E80 <= cp && cp <= 0x303E) || /* CJK */
            (0x3040 <= cp && cp <= 0x33BF) || /* CJK / Kana / Hangul */
            (0x3400 <= cp && cp <= 0x4DBF) ||
            (0x4E00 <= cp && cp <= 0xA4CF) ||
            (0xAC00 <= cp && cp <= 0xD7AF) ||
            (0xF900 <= cp && cp <= 0xFAFF) ||
            (0xFE30 <= cp && cp <= 0xFE6F) ||
            (0xFF01 <= cp && cp <= 0xFF60) || /* 全角 */
            (0xFFE0 <= cp && cp <= 0xFFE6) ||
            (cp >= 0x20000)
        )) {
            w += 2;
        } else {
            w += 1;
        }
    }
    return w;
}

/**
 * pad_to — 将字符串填充到指定终端显示宽度，写入调用者提供的缓冲区
 * 这是线程安全版本，推荐使用。
 */
void pad_to(char *buf, int bufsize, const char *text, int width) {
    if (!text) text = "";
    int dw = display_width(text);
    if (dw >= width) {
        snprintf(buf, bufsize, "%s", text);
    } else {
        int spaces = width - dw;
        snprintf(buf, bufsize, "%s%*s", text, spaces, "");
    }
}

/**
 * color_wrap — 将文本包裹 ANSI 转义码，写入调用者提供的缓冲区
 */
void color_wrap(char *buf, int bufsize, const char *code, const char *text) {
    if (!text) text = "";
    /* code="0" 表示 reset/默认色，不需要包裹 ANSI 转义码 */
    if (!g_no_color && _color_is_tty && strcmp(code, "0") != 0)
        snprintf(buf, bufsize, "\033[%sm%s\033[0m", code, text);
    else
        snprintf(buf, bufsize, "%s", text);
}

/**
 * color_pad — 先 pad 再上色，写入调用者提供的缓冲区
 * 这个函数组合了 pad_to + color_wrap，避免两次调用共享静态缓冲区
 */
void color_pad(char *buf, int bufsize, const char *code, const char *text, int width) {
    char tmp[MAX_STR * 2];
    pad_to(tmp, sizeof(tmp), text, width);
    color_wrap(buf, bufsize, code, tmp);
}

/*
 * 以下函数为了兼容性保留，使用独立静态缓冲区（每个函数一个）。
 * 注意: 同一 printf 中只能安全使用每个函数一次！
 * 如果需要多次使用，请用上面的 pad_to/color_wrap/color_pad。
 */

static char _pad_buf[MAX_STR * 2];
char *pad_cjk(const char *text, int width) {
    pad_to(_pad_buf, sizeof(_pad_buf), text, width);
    return _pad_buf;
}

static char _red_buf[MAX_STR * 4];
const char *color_red(const char *t) {
    color_wrap(_red_buf, sizeof(_red_buf), "31", t);
    return _red_buf;
}

static char _green_buf[MAX_STR * 4];
const char *color_green(const char *t) {
    color_wrap(_green_buf, sizeof(_green_buf), "32", t);
    return _green_buf;
}

static char _yellow_buf[MAX_STR * 4];
const char *color_yellow(const char *t) {
    color_wrap(_yellow_buf, sizeof(_yellow_buf), "33", t);
    return _yellow_buf;
}

static char _blue_buf[MAX_STR * 4];
const char *color_blue(const char *t) {
    color_wrap(_blue_buf, sizeof(_blue_buf), "34", t);
    return _blue_buf;
}

static char _magenta_buf[MAX_STR * 4];
const char *color_magenta(const char *t) {
    color_wrap(_magenta_buf, sizeof(_magenta_buf), "35", t);
    return _magenta_buf;
}

static char _cyan_buf[MAX_STR * 4];
const char *color_cyan(const char *t) {
    color_wrap(_cyan_buf, sizeof(_cyan_buf), "36", t);
    return _cyan_buf;
}

static char _bold_buf[MAX_STR * 4];
const char *color_bold(const char *t) {
    color_wrap(_bold_buf, sizeof(_bold_buf), "1", t);
    return _bold_buf;
}

static char _dim_buf[MAX_STR * 4];
const char *color_dim(const char *t) {
    color_wrap(_dim_buf, sizeof(_dim_buf), "2", t);
    return _dim_buf;
}

static char _cpad_buf[MAX_STR * 4];
const char *color_cpad(const char *code, const char *text, int width) {
    color_pad(_cpad_buf, sizeof(_cpad_buf), code, text, width);
    return _cpad_buf;
}
