# ============================================================================
# Stage 1: vcpkg-deps - 预编译 vcpkg 依赖
# ============================================================================
FROM debian:bookworm AS vcpkg-deps

# 安装构建工具和依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    zip \
    unzip \
    tar \
    pkg-config \
    ca-certificates \
    ninja-build \
    # 运行时库开发包
    libpq-dev \
    libhiredis-dev \
    # 其他依赖
    linux-libc-dev \
    && rm -rf /var/lib/apt/lists/*

# 设置工作目录
WORKDIR /build

# 复制 vcpkg 配置
COPY vcpkg.json .

# 克隆并引导 vcpkg
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone --depth 1 https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} && \
    ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics

# 安装仅服务端需要的依赖（排除客户端依赖）
RUN ${VCPKG_ROOT}/vcpkg install --triplet=x64-linux

# ============================================================================
# Stage 2: builder - 编译所有服务可执行文件
# ============================================================================
FROM vcpkg-deps AS builder

# 复制源代码
COPY . /build/src

WORKDIR /build/src

# 配置和构建项目
RUN cmake -B build \
    -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    -DBUILD_SERVER=ON \
    -DBUILD_DB=ON \
    -DBUILD_CLIENT=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_BENCHMARKS=OFF \
    -DBUILD_TOOLS=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/mir2 \
    && cmake --build build --config Release -j$(nproc) \
    && cmake --install build

# 验证可执行文件
RUN ls -lh /opt/mir2/bin/ && \
    ldd /opt/mir2/bin/mir2_gateway && \
    ldd /opt/mir2/bin/mir2_world && \
    ldd /opt/mir2/bin/mir2_game && \
    ldd /opt/mir2/bin/mir2_db

# ============================================================================
# Stage 3: gateway - Gateway 服务最小运行时镜像
# ============================================================================
FROM debian:bookworm-slim AS gateway

# 安装运行时依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    libhiredis0.14 \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 创建非 root 用户
RUN groupadd -r mir2 && useradd -r -g mir2 mir2

# 创建必要的目录
RUN mkdir -p /opt/mir2/bin /opt/mir2/config /opt/mir2/logs && \
    chown -R mir2:mir2 /opt/mir2

# 从 builder 复制可执行文件和配置
COPY --from=builder --chown=mir2:mir2 /opt/mir2/bin/mir2_gateway /opt/mir2/bin/
COPY --from=builder --chown=mir2:mir2 /build/src/config /opt/mir2/config/

WORKDIR /opt/mir2

# 切换到非 root 用户
USER mir2:mir2

# 暴露端口（根据实际配置调整）
EXPOSE 7000

CMD ["/opt/mir2/bin/mir2_gateway"]

# ============================================================================
# Stage 4: world - World 服务最小运行时镜像
# ============================================================================
FROM debian:bookworm-slim AS world

# 安装运行时依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    libhiredis0.14 \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 创建非 root 用户
RUN groupadd -r mir2 && useradd -r -g mir2 mir2

# 创建必要的目录
RUN mkdir -p /opt/mir2/bin /opt/mir2/config /opt/mir2/logs /opt/mir2/data && \
    chown -R mir2:mir2 /opt/mir2

# 从 builder 复制可执行文件和配置
COPY --from=builder --chown=mir2:mir2 /opt/mir2/bin/mir2_world /opt/mir2/bin/
COPY --from=builder --chown=mir2:mir2 /build/src/config /opt/mir2/config/

WORKDIR /opt/mir2

# 切换到非 root 用户
USER mir2:mir2

# 暴露端口（根据实际配置调整）
EXPOSE 7100

CMD ["/opt/mir2/bin/mir2_world"]

# ============================================================================
# Stage 5: game - Game 服务最小运行时镜像
# ============================================================================
FROM debian:bookworm-slim AS game

# 安装运行时依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    libhiredis0.14 \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 创建非 root 用户
RUN groupadd -r mir2 && useradd -r -g mir2 mir2

# 创建必要的目录
RUN mkdir -p /opt/mir2/bin /opt/mir2/config /opt/mir2/logs /opt/mir2/data && \
    chown -R mir2:mir2 /opt/mir2

# 从 builder 复制可执行文件和配置
COPY --from=builder --chown=mir2:mir2 /opt/mir2/bin/mir2_game /opt/mir2/bin/
COPY --from=builder --chown=mir2:mir2 /build/src/config /opt/mir2/config/

WORKDIR /opt/mir2

# 切换到非 root 用户
USER mir2:mir2

# 暴露端口（根据实际配置调整）
EXPOSE 7200

CMD ["/opt/mir2/bin/mir2_game"]

# ============================================================================
# Stage 6: db - DB 服务最小运行时镜像
# ============================================================================
FROM debian:bookworm-slim AS db

# 安装运行时依赖
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 \
    libhiredis0.14 \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 创建非 root 用户
RUN groupadd -r mir2 && useradd -r -g mir2 mir2

# 创建必要的目录
RUN mkdir -p /opt/mir2/bin /opt/mir2/config /opt/mir2/logs && \
    chown -R mir2:mir2 /opt/mir2

# 从 builder 复制可执行文件和配置
COPY --from=builder --chown=mir2:mir2 /opt/mir2/bin/mir2_db /opt/mir2/bin/
COPY --from=builder --chown=mir2:mir2 /build/src/config /opt/mir2/config/

WORKDIR /opt/mir2

# 切换到非 root 用户
USER mir2:mir2

# 暴露端口（根据实际配置调整）
EXPOSE 7300

CMD ["/opt/mir2/bin/mir2_db"]
