# -*- coding: utf-8 -*-
"""
color.py — ANSI 彩色终端输出

提供 Color 类，支持 --no-color 关闭彩色输出。
本模块无外部依赖，仅依赖标准库 sys。
"""

import sys


def display_width(text):
    """计算字符串在终端的显示宽度（中文/全角=2，ASCII=1）"""
    w = 0
    for ch in text:
        cp = ord(ch)
        if cp >= 0x1100 and (
            cp <= 0x115F or  # Hangul Jamo
            0x2329 <= cp <= 0x232A or
            0x2E80 <= cp <= 0x303E or  # CJK
            0x3040 <= cp <= 0x33BF or  # CJK / Kana / Hangul
            0x3400 <= cp <= 0x4DBF or
            0x4E00 <= cp <= 0xA4CF or
            0xAC00 <= cp <= 0xD7AF or
            0xF900 <= cp <= 0xFAFF or
            0xFE30 <= cp <= 0xFE6F or
            0xFF01 <= cp <= 0xFF60 or  # 全角
            0xFFE0 <= cp <= 0xFFE6 or
            0x20000 <= cp
        ):
            w += 2
        else:
            w += 1
    return w


def pad_cjk(text, width):
    """将字符串填充到指定终端显示宽度（中文=2列）"""
    dw = display_width(text)
    if dw >= width:
        return text
    return text + " " * (width - dw)


class Color:
    """终端颜色控制，支持 --no-color 关闭"""
    ENABLED = True

    @classmethod
    def _wrap(cls, code, text):
        if cls.ENABLED and sys.stdout.isatty():
            return f"\033[{code}m{text}\033[0m"
        return text

    @staticmethod
    def _cpad(code, text, width):
        """先 pad 到指定终端宽度，再上色，避免转义码影响对齐"""
        padded = pad_cjk(text, width)
        if Color.ENABLED and sys.stdout.isatty():
            return f"\033[{code}m{padded}\033[0m"
        return padded

    @classmethod
    def red(cls, t):    return cls._wrap("31", t)
    @classmethod
    def green(cls, t):  return cls._wrap("32", t)
    @classmethod
    def yellow(cls, t): return cls._wrap("33", t)
    @classmethod
    def blue(cls, t):   return cls._wrap("34", t)
    @classmethod
    def magenta(cls, t): return cls._wrap("35", t)
    @classmethod
    def cyan(cls, t):   return cls._wrap("36", t)
    @classmethod
    def bold(cls, t):   return cls._wrap("1", t)
    @classmethod
    def dim(cls, t):    return cls._wrap("2", t)
