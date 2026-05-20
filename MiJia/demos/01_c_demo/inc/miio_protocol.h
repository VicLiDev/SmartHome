/*
 * miio_protocol.h — miIO 协议常量与数据结构
 *
 * 定义了 miIO 协议的所有核心类型，供网关所有模块使用。
 */

#ifndef MIIO_PROTOCOL_H
#define MIIO_PROTOCOL_H

#include <stdint.h>

/* ═══ 协议常量 ═══ */
#define MIIO_PORT           54321
#define MIIO_MULTICAST      "224.0.0.50"
#define MIIO_MAGIC          0x2131
#define MIIO_HEADER_SIZE    60       /* 2+2+4+4+16+32 */
#define NONCE_SIZE          16
#define SIGNATURE_SIZE      32
#define TOKEN_HEX_LEN       32        /* 32 字符 = 128 bit */
#define MAX_PAYLOAD_SIZE    16384
#define MAX_DEVICE_COUNT    64
#define MAX_IP_STRLEN       16
#define MAX_MODEL_LEN       64
#define MAX_PATH_LEN        256
#define MAX_URL_LEN         512
#define MAX_LOG_MSG_LEN     256

/* ═══ 数据结构 ═══ */

/** 设备信息 */
typedef struct {
    char         ip[MAX_IP_STRLEN];
    uint16_t     port;
    uint32_t     device_id;
    char         model[MAX_MODEL_LEN];
    char         token[TOKEN_HEX_LEN + 1];   /* 含 \0 终止符 */
    uint32_t     last_seen;                  /* Unix 时间戳 */
    int          online;                      /* 1=在线 0=离线 */
} MiioDevice;

/** miIO 报文头（网络字节序布局） */
typedef struct __attribute__((packed)) {
    uint16_t     magic;
    uint16_t     length;
    uint32_t     device_id;
    uint32_t     timestamp;
    uint8_t      nonce[NONCE_SIZE];
    uint8_t      signature[SIGNATURE_SIZE];
} MiioHeader;

/** JSON-RPC 请求 */
typedef struct {
    int          id;
    char         method[128];
    char        *params_json;          /* 堆分配，需 free */
} MiioRequest;

/** JSON-RPC 响应 */
typedef struct {
    int          id;
    int          error_code;            /* 0=成功 */
    char         error_msg[256];
    char        *result_json;           /* 堆分配，需 free */
} MiioResponse;

/** 网关配置 */
typedef struct {
    int          listen_port;           /* API 监听端口 */
    int          scan_timeout;           /* 扫描超时（秒） */
    int          cmd_timeout;            /* 命令超时（秒） */
    char         config_path[MAX_PATH_LEN];
    char         db_path[MAX_PATH_LEN];  /* 设备数据库路径 */
    int          verbose;                /* 详细日志 */
} GatewayConfig;

/** 密钥三元组（从 token 派生） */
typedef struct {
    unsigned char aes_key[16];          /* MD5(token) */
    unsigned char aes_iv[16];           /* MD5(aes_key + token) */
    unsigned char sign_key[16];         /* MD5(aes_key + iv + aes_key) */
} MiioKeys;

#endif /* MIIO_PROTOCOL_H */
