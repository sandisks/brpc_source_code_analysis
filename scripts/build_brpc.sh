#!/usr/bin/env bash
# 一键搭建 brpc 1.17.0 本地依赖并编译本仓库 examples
# 用法: bash scripts/build_brpc.sh [--skip-brpc]
set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${BRPC_ROOT:-/tmp/brpc_local/usr}"
LD_LIBRARY_PATH="${PREFIX}/lib/x86_64-linux-gnu:${LD_LIBRARY_PATH}"
export LD_LIBRARY_PATH

echo "==> [1/4] 下载依赖 deb 包（protobuf/gflags/openssl/leveldb）"
DEPS_DIR=/tmp/brpc_deps
mkdir -p "$DEPS_DIR" && cd "$DEPS_DIR"
for pkg in libprotobuf-dev protobuf-compiler libprotobuf32t64 libprotoc32t64 \
           libgflags-dev libgflags2.2 libssl-dev zlib1g-dev \
           libleveldb-dev libleveldb1d; do
    [ -f "${pkg}."*.deb ] 2>/dev/null || apt-get download -qq "$pkg" >/dev/null 2>&1 || true
done
rm -rf /tmp/brpc_local && mkdir -p /tmp/brpc_local
for d in *.deb; do [ -f "$d" ] && dpkg -x "$d" /tmp/brpc_local; done
# 补齐链接器符号链接
cd "$PREFIX/lib/x86_64-linux-gnu"
ln -sf libprotoc.so.32 libprotoc.so
ln -sf libprotobuf.so.32 libprotobuf.so
# 补齐 multiarch 的 openssl 配置头（注意：必须用 cp，符号链接相对路径会解析错）
cd "$PREFIX/include/openssl"
rm -f opensslconf.h configuration.h
cp ../x86_64-linux-gnu/openssl/opensslconf.h ./opensslconf.h
cp ../x86_64-linux-gnu/openssl/configuration.h ./configuration.h

if [ "$1" != "--skip-brpc" ]; then
    echo "==> [2/4] 下载并编译 brpc 1.17.0"
    SRC=/tmp/brpc_src/brpc-1.17.0-src
    if [ ! -d "$SRC" ]; then
        mkdir -p /tmp/brpc_src && cd /tmp/brpc_src
        curl -sL --retry 3 -o brpc.tar.gz \
            https://github.com/apache/brpc/archive/refs/tags/1.17.0.tar.gz
        tar xzf brpc.tar.gz
    fi
    mkdir -p "$SRC/build" && cd "$SRC/build"
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_PREFIX_PATH=/tmp/brpc_local/usr \
          -DCMAKE_INSTALL_PREFIX="$PREFIX" .. >/tmp/brpc_cmake.log 2>&1
    make -j"$(nproc)" >/tmp/brpc_make.log 2>&1
    make install >/tmp/brpc_install.log 2>&1
    echo "    brpc 安装到 $PREFIX"
fi

echo "==> [3/4] 配置并编译 examples（CMake）"
BUILD="$ROOT/build"
cmake -S "$ROOT" -B "$BUILD" -DBRPC_ROOT="$PREFIX" \
      -DCMAKE_BUILD_TYPE=Release >/tmp/ex_cmake.log 2>&1
cmake --build "$BUILD" -j"$(nproc)" >>/tmp/ex_make.log 2>&1
echo "    可执行文件在 $BUILD/ 下"

echo "==> [4/4] 运行例子并写日志"
mkdir -p "$ROOT/build_logs"
bash "$ROOT/scripts/run_examples.sh"
echo "==> 完成。日志见 $ROOT/build_logs/"
