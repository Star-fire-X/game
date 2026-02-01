#!/bin/bash
#
# MIR2 Docker 构建脚本
# 用法: ./build-docker.sh [选项] [服务名]
#

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 打印信息
info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

# 显示帮助信息
show_help() {
    cat << EOF
MIR2 Docker 构建脚本

用法:
    $0 [选项] [服务名]

服务名:
    all         - 构建所有服务（默认）
    gateway     - 仅构建 Gateway 服务
    world       - 仅构建 World 服务
    game        - 仅构建 Game 服务
    db          - 仅构建 DB 服务

选项:
    -h, --help              显示帮助信息
    -c, --clean             清理构建缓存
    -n, --no-cache          不使用构建缓存
    -p, --push              构建后推送到镜像仓库
    -t, --tag TAG           指定镜像标签（默认: latest）
    -r, --registry REGISTRY 指定镜像仓库前缀

示例:
    $0                      # 构建所有服务
    $0 gateway              # 仅构建 Gateway
    $0 -n all               # 不使用缓存构建所有服务
    $0 -t v1.0.0 gateway    # 构建 Gateway 并标记为 v1.0.0
    $0 -r myregistry.com/mir2 -p all  # 构建并推送到私有仓库

EOF
}

# 默认配置
CLEAN=false
NO_CACHE=false
PUSH=false
TAG="latest"
REGISTRY=""
SERVICE="all"

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -n|--no-cache)
            NO_CACHE=true
            shift
            ;;
        -p|--push)
            PUSH=true
            shift
            ;;
        -t|--tag)
            TAG="$2"
            shift 2
            ;;
        -r|--registry)
            REGISTRY="$2"
            shift 2
            ;;
        all|gateway|world|game|db)
            SERVICE="$1"
            shift
            ;;
        *)
            error "未知参数: $1\n使用 -h 查看帮助"
            ;;
    esac
done

# 构建选项
BUILD_OPTS=""
if [ "$NO_CACHE" = true ]; then
    BUILD_OPTS="--no-cache"
fi

# 镜像名称前缀
IMAGE_PREFIX="${REGISTRY:+$REGISTRY/}mir2"

# 清理函数
clean_build() {
    info "清理 Docker 构建缓存..."
    docker builder prune -f
    info "清理完成"
}

# 构建单个服务
build_service() {
    local service=$1
    local image_name="${IMAGE_PREFIX}-${service}:${TAG}"

    info "构建 ${service} 服务..."
    info "镜像名称: ${image_name}"

    docker build \
        ${BUILD_OPTS} \
        --target ${service} \
        -t ${image_name} \
        -t ${IMAGE_PREFIX}-${service}:latest \
        .

    info "✓ ${service} 构建完成"

    if [ "$PUSH" = true ]; then
        info "推送 ${image_name} 到仓库..."
        docker push ${image_name}
        docker push ${IMAGE_PREFIX}-${service}:latest
        info "✓ ${service} 推送完成"
    fi
}

# 构建所有服务
build_all() {
    info "开始构建所有服务..."
    build_service "gateway"
    build_service "world"
    build_service "game"
    build_service "db"
    info "✓ 所有服务构建完成"
}

# 主流程
main() {
    info "MIR2 Docker 构建脚本"
    info "===================="

    if [ "$CLEAN" = true ]; then
        clean_build
    fi

    case $SERVICE in
        all)
            build_all
            ;;
        gateway|world|game|db)
            build_service $SERVICE
            ;;
        *)
            error "未知服务: $SERVICE"
            ;;
    esac

    info "===================="
    info "构建完成！"

    # 显示镜像信息
    info "已构建的镜像:"
    docker images | grep "${IMAGE_PREFIX}" | head -10
}

# 执行主流程
main
