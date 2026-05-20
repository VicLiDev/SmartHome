#!/bin/bash
# prjBuild.sh — Xiaomi Gateway 构建脚本
#
# 用法:
#   ./prjBuild.sh [命令] [选项]
#
# 命令:
#   check-deps   检查编译依赖
#   build        编译项目（默认）
#   run          编译并运行
#   clean        清理构建产物
#   install      安装到系统
#   cross-aarch64 交叉编译 ARM64
#   help         显示帮助

set -e

# ═══ 路径配置 ═══
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR"
BUILD_DIR="$PROJECT_DIR/build"
BIN_DIR="$PROJECT_DIR/bin"

# 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC} $*"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*"; }

# ═══ check-deps ═══

cmd_check_deps() {
    log_info "检查编译依赖..."

    local missing=0

    # 检查 C 编译器
    if command -v gcc &>/dev/null; then
        log_info "  ✓ gcc: $(gcc --version | head -1)"
    else
        log_error "  ✗ gcc 未安装"
        missing=1
    fi

    # 检查 OpenSSL 开发库
    if pkg-config --exists openssl 2>/dev/null || \
       [ -f /usr/include/openssl/md5.h ]; then
        log_info "  ✓ OpenSSL (开发头文件)"
        pkg-config --modversion openssl 2>/dev/null || true
    else
        log_error "  ✗ OpenSSL 开发库未安装 (sudo apt install libssl-dev)"
        missing=1
    fi

    # 检查 pthread
    if [ -f /usr/include/pthread.h ]; then
        log_info "  ✓ pthread"
    else
        log_error "  ✗ pthread 头文件缺失"
        missing=1
    fi

    # 检查 cJSON
    if [ -f "$PROJECT_DIR/third_party/cJSON/cJSON.h" ]; then
        log_info "  ✓ cJSON ($(wc -l < "$PROJECT_DIR/third_party/cJSON/cJSON.c") 行)"
    else
        log_warn "  ⚠ cJSON 缺失，将自动下载..."
        mkdir -p "$PROJECT_DIR/third_party/cJSON"
        cd "$PROJECT_DIR/third_party/cJSON" && \
            wget -q "https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.h" -O cJSON.h && \
            wget -q "https://raw.githubusercontent.com/DaveGamble/cJSON/master/cJSON.c" -O cJSON.c && \
            log_info "  ✓ cJSON 已下载"
    fi

    # 检查交叉编译工具链（可选）
    if command -v aarch64-linux-gnu-gcc &>/dev/null; then
        log_info "  ✓ aarch64-linux-gnu-gcc (可选，用于 RK 板子)"
    else
        log_warn "  ○ aarch64 交叉编译工具链未安装 (sudo apt install gcc-aarch64-linux-gnu)"
    fi

    if [ $missing -ne 0 ]; then
        log_error "有缺失依赖，请安装后重试"
        return 1
    fi

    log_info "所有依赖已满足 ✅"
}

# ═══ build ═══

cmd_build() {
    local arch="${1:-host}"
    log_info "开始编译 (ARCH=$arch)..."

    mkdir -p "$BUILD_DIR" "$BIN_DIR"

    if [ "$arch" = "aarch64" ]; then
        make ARCH=aarch64 -C "$PROJECT_DIR"
    else
        make -C "$PROJECT_DIR"
    fi
}

# ═══ run ═══

cmd_run() {
    cmd_build "${1:-host}"

    local target="$BIN_DIR/miio_gateway"
    if [ ! -x "$target" ]; then
        log_error "编译产物不存在: $target"
        return 1
    fi

    log_info "运行扫描测试..."
    "$target" scan --timeout 5
}

# ═══ clean ═══

cmd_clean() {
    log_info "清理构建产物..."
    make -C "$PROJECT_DIR" clean
    log_info "清理完成 ✅"
}

# ═══ install ═══

cmd_install() {
    log_info "安装到系统..."
    make -C "$PROJECT_DIR" install
    log_info "安装完成 ✅"
}

# ═══ help ═══

cmd_help() {
    cat << 'EOF'
Xiaomi MIoT Gateway 构建脚本

用法: ./prjBuild.sh <命令> [选项]

命令:
  check-deps          检查编译依赖
  build [ARCH]        编译项目 (默认 host, 可选 aarch64)
  run [ARCH]          编译并运行扫描测试
  clean               清理构建产物
  install             安装到系统 (/usr/local/bin)
  cross-aarch64       交叉编译 ARM64 (用于 Rockchip 板子)
  help                显示本帮助

示例:
  ./prjBuild.sh                    # 默认编译
  ./prjBuild.sh run                # 编译 + 运行测试
  ./prjBuild.sh cross-aarch64      # 交叉编译给 RK3588 用
  ./prjBuild.sh check-deps         # 检查环境

文档: 见 README.md
EOF
}

# ═══ 主入口 ═══

main() {
    cd "$PROJECT_DIR"

    case "${1:-build}" in
        check-deps)  cmd_check_deps ;;
        build)       cmd_build "${2:-host}" ;;
        run)         cmd_run "${2:-host}" ;;
        clean)       cmd_clean ;;
        install)     cmd_install ;;
        cross-aarch64) cmd_build aarch64 ;;
        help|-h|--help) cmd_help ;;
        *)
            log_error "未知命令: $1"
            cmd_help
            exit 1
            ;;
    esac
}

main "$@"
