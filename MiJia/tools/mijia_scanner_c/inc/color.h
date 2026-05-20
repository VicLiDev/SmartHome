/*
 * color.h — ANSI 彩色终端输出 + CJK 字符宽度
 *
 * 提供 ANSI 颜色控制、CJK 字符宽度计算、终端对齐填充。
 * 支持通过全局变量禁用彩色输出。
 */

#ifndef Mijia_COLOR_H
#define Mijia_COLOR_H

#include <stddef.h>

/* ═══ 颜色开关 ═══ */
extern int color_enabled;

/* 初始化颜色（根据 isatty 自动判断） */
void color_init(void);

/* 禁用彩色输出 */
void color_disable(void);

/* ═══ ANSI 颜色包装 ═══ */
const char *color_red(const char *text);
const char *color_green(const char *text);
const char *color_yellow(const char *text);
const char *color_blue(const char *text);
const char *color_magenta(const char *text);
const char *color_cyan(const char *text);
const char *color_bold(const char *text);
const char *color_dim(const char *text);

/* 带颜色的 CJK 对齐填充（先 pad 再上色，避免转义码影响宽度） */
const char *color_cpad(const char *code, const char *text, int width);

/* ═══ CJK 字符宽度 ═══ */

/**
 * 计算字符串在终端的显示宽度（中文/全角=2，ASCII=1）
 */
int display_width(const char *text);

/**
 * 将字符串填充到指定终端显示宽度（中文=2列），返回静态缓冲区
 */
const char *pad_cjk(const char *text, int width);

/**
 * 截断字符串到指定终端显示宽度，超长加 ".."
 */
const char *truncate_cjk(const char *text, int max_width);

#endif /* Mijia_COLOR_H */
