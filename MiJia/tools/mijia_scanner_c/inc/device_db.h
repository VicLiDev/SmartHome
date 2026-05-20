/*
 * device_db.h — 内置设备型号数据库
 *
 * 根据设备型号前缀查询中文名称和设备类型。
 * 包含 50+ 常见小米生态链设备条目。
 */

#ifndef Mijia_DEVICE_DB_H
#define Mijia_DEVICE_DB_H

/* 型号数据库条目 */
typedef struct {
    const char *prefix;   /* 型号前缀 */
    const char *name;     /* 中文名称 */
    const char *type;     /* 设备类型 */
} DeviceEntry;

/* 品牌反查条目 */
typedef struct {
    const char *brand;    /* 品牌关键字 */
    const char *name;     /* 品牌名称 */
    const char *type;     /* 默认类型 */
} BrandEntry;

/**
 * 根据型号查询中文名称和类型
 *
 * @param model  设备型号字符串（可为 NULL 或 "unknown"）
 * @param name_out  输出名称缓冲区（需 >= 128 字节）
 * @param type_out  输出类型缓冲区（需 >= 64 字节）
 */
void lookup_device(const char *model, char *name_out, char *type_out);

/**
 * 打印完整型号数据库（按类型分组）
 */
void print_device_database(void);

/**
 * 获取数据库条目总数
 */
int device_db_count(void);

#endif /* Mijia_DEVICE_DB_H */
