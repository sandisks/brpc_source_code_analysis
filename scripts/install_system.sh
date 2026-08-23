#!/usr/bin/env bash
# 把 brpc 1.17.0 及依赖（protobuf/gflags/openssl/leveldb）安装到系统目录 /usr/local
# 需要 sudo（会提示输入密码），安装后重启不丢失，CLion 可直接使用
# 用法: bash scripts/install_system.sh
set -e

PREFIX=/usr/local

echo "==> [1/4] 下载依赖 deb 包并解包到临时目录"
DEPS_DIR=/tmp/brpc_deps_sys
rm -rf "$DEPS_DIR" && mkdir -p "$DEPS_DIR" && cd "$DEPS_DIR"
for pkg in libprotobuf-dev protobuf-compiler libprotobuf32t64 libprotoc32t64 \
           libgflags-dev libgflags2.2 libssl-dev zlib1g-dev \
           libleveldb-dev libleveldb1d libsnappy-dev libsnappy1v5; do
    apt-get download -qq "$pkg" >/dev/null 2>&1 || true
done
rm -rf /tmp/brpc_local_sys && mkdir -p /tmp/brpc_local_sys
for d in *.deb; do [ -f "$d" ] && dpkg -x "$d" /tmp/brpc_local_sys; done
# 补齐链接器符号链接
cd /tmp/brpc_local_sys/usr/lib/x86_64-linux-gnu
ln -sf libprotoc.so.32 libprotoc.so
ln -sf libprotobuf.so.32 libprotobuf.so
# 补齐 multiarch 的 openssl 配置头（必须用 cp，符号链接相对路径会解析错）
cd /tmp/brpc_local_sys/usr/include/openssl
rm -f opensslconf.h configuration.h
cp ../x86_64-linux-gnu/openssl/opensslconf.h ./opensslconf.h
cp ../x86_64-linux-gnu/openssl/configuration.h ./configuration.h

echo "==> [2/4] sudo 复制到 /usr/local（将提示输入密码）"
sudo mkdir -p "$PREFIX/include" "$PREFIX/lib/x86_64-linux-gnu" "$PREFIX/bin"
sudo cp -r /tmp/brpc_local_sys/usr/include/. "$PREFIX/include/"
sudo cp -r /tmp/brpc_local_sys/usr/lib/x86_64-linux-gnu/. "$PREFIX/lib/x86_64-linux-gnu/"
sudo cp /tmp/brpc_local_sys/usr/bin/protoc "$PREFIX/bin/" 2>/dev/null || true
sudo ldconfig
echo "    依赖已复制到 $PREFIX"

echo "==> [3/4] 下载并编译安装 brpc 1.17.0 到 $PREFIX"
SRC=/tmp/brpc_src/brpc-1.17.0-src
if [ ! -d "$SRC" ]; then
    mkdir -p /tmp/brpc_src && cd /tmp/brpc_src
    curl -sL --retry 3 -o brpc.tar.gz \
        https://github.com/apache/brpc/archive/refs/tags/1.17.0.tar.gz
    tar xzf brpc.tar.gz
fi
mkdir -p "$SRC/build-sys" && cd "$SRC/build-sys"
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" \
      -DCMAKE_PREFIX_PATH=/usr/local .. >/tmp/brpc_sys_cmake.log 2>&1
make -j"$(nproc)" >/tmp/brpc_sys_make.log 2>&1
sudo make install >/tmp/brpc_sys_install.log 2>&1
sudo ldconfig
echo "    brpc 已安装到 $PREFIX"

echo "==> [4/4] 完成。重启后无需重新安装；CMakeLists 默认会自动探测 /usr/local"
echo "    若失败，检查日志：/tmp/brpc_sys_cmake.log /tmp/brpc_sys_make.log"
