/*
 * output.h — 表格打印 + JSON/CSV 导出
 *
 * 提供设备列表的彩色表格打印、JSON 导出、CSV 导出功能。
 */

#ifndef Mijia_OUTPUT_H
#define Mijia_OUTPUT_H

#include "protocol.h"

/**
 * 彩色表格打印设备列表
 *
 * @param devices  设备数组
 * @param count    设备数量
 */
void print_device_table(const ScanDevice *devices, int count);

/**
 * 导出 JSON 格式（到文件或 stdout）
 *
 * @param devices  设备数组
 * @param count    设备数量
 * @param output   输出文件路径（NULL 则输出到 stdout）
 * @return 0 成功, -1 失败
 */
int export_json(const ScanDevice *devices, int count, const char *output);

/**
 * 导出 CSV 格式（到文件或 stdout）
 *
 * @param devices  设备数组
 * @param count    设备数量
 * @param output   输出文件路径（NULL 则输出到 stdout）
 * @return 0 成功, -1 失败
 */
int export_csv(const ScanDevice *devices, int count, const char *output);

#endif /* Mijia_OUTPUT_H */
