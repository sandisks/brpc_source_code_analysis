# butil 独立 CMake 库

把 brpc 中的 **butil**（基础工具库，brpc 的"底座"）抽取为独立的 CMake 静态库。**butil 原代码已随本仓库 git 管理（brpc 1.17.0）**，构建不依赖外部 brpc 源码树。

> **third_party 是 git submodule**：`src/butil/third_party/` 由独立仓库
> `third_party_butil.git` 管理（官方 apache/brpc 是普通目录，此处按需拆分为 submodule）。
> clone 后需 `git submodule update --init` 拉取。

## 为什么能独立成库

- butil 源码中**零** `#include "bthread/..."` 或 `#include "brpc/..."`（已验证），它是纯基础库
- 外部依赖极小：**gflags + openssl + protobuf + pthread**（protobuf 因 IOBuf 实现 `ZeroCopyInput/OutputStream` 接口，需要头文件 + 链接库）
- 官方 CMake 本来就是 `BUTIL_LIB`（OBJECT library）+ `SOURCES_LIB` + `PROTO_LIB` 三段式，最后合成 `libbrpc.a`；本工程把第一段独立出来

## 目录结构

```
butil_cmake_lib/
├── CMakeLists.txt        # 主构建：生成 libbutil.a + deb 打包 (CPack)
├── config.h.in           # 模板，configure_file 生成 butil/config.h
├── src/
│   └── butil/            # butil 原代码（brpc 1.17.0，git 管理）
│       ├── *.cpp/*.cc/*.c/*.h
│       └── third_party/  # git submodule（独立仓库 third_party_butil.git）
│                        # dmg_fp / snappy / symbolize / rapidjson 等内置第三方
├── demo/
│   ├── CMakeLists.txt
│   └── main.cpp          # 验证 IOBuf / EndPoint / flat_map / 日志 / 随机数
└── README.md
```

## 构建与运行

```bash
git submodule update --init        # 首次 clone 后拉取 third_party
cmake -B build
cmake --build build -j$(nproc)
./build/demo/butil_demo
```

如需使用其他版本的 butil 源码，覆盖源码目录即可：
`cmake -B build -DBUTIL_SRC_DIR=/path/to/brpc/src/butil`

## 打包 deb

```bash
cd build && cpack -G DEB          # 生成 build/libbutil-1.17.0-Linux.deb
dpkg-deb -I libbutil-1.17.0-Linux.deb   # 查看元信息
dpkg-deb -c libbutil-1.17.0-Linux.deb   # 查看内容（/usr/include/butil/** + /usr/lib/libbutil.a）
```

## third_party submodule 说明

- 独立仓库：`third_party_butil.git`（本地位于 `~/git/cpp/`，commit `55c6618`）
- `.gitmodules` 使用相对 URL `../third_party_butil.git`（与主仓库同一命名空间，
  推到 GitHub 后语义成立）；本地 `.git/config` 用 `file://` 路径覆盖，保证本地可用
- 他人 clone 需同时取得该仓库，或修改 `.gitmodules`/本地 config 中的 URL

## 与官方 CMakeLists.txt 的对应关系

| 本工程步骤 | 官方对应（brpc 顶层 CMakeLists.txt） |
|---|---|
| `configure_file` 生成 `butil/config.h` | 第 114 行，`config.h.in` → `src/butil/config.h` |
| 编译宏（`_GNU_SOURCE` 等） | 第 147 行 `CMAKE_CPP_FLAGS` |
| 源文件 GLOB + 黑名单 | 第 370-503 行 `BUTIL_SOURCES`（Linux/Darwin 分支） |
| 链接 gflags/openssl/protobuf/pthread/rt/dl | 第 298-344 行 `DYNAMIC_LIB` |

## 源文件收集规则（GLOB 黑名单）

GLOB 全量收集后需排除（对照官方 `BUTIL_SOURCES`，防止误收）：

- `third_party/dmg_fp/dtoa.cc` — 实现由 `dtoa_wrapper.cc` 以 `#include` 引入，重复编译会符号冲突
- `comlog_sink.cc` — 依赖百度 comlog 库，官方未编译
- 非 Apple 平台：`*.mm`、`mac/` 目录、`*_mac.cc`、`*_freebsd.cc`（官方仅在 Darwin/Linux 分支引入对应文件）

## 踩坑记录

1. **protobuf 必须链接**：IOBuf 继承 protobuf 的 `ZeroCopyOutputStream`，编译期没问题，链接期报 `undefined reference to vtable for ...ZeroCopyOutputStream` —— 需加 `libprotobuf`
2. **symbolize 的 config.h**：`third_party/symbolize/` 自带 `config.h`（Chromium 移植），无需生成
3. **`butil/config.h` 需生成**：`logging.h` 依赖 `#include "butil/config.h"`（定义 `BRPC_WITH_GLOG`），模板同官方 `config.h.in`

## 可选：安装

```bash
cmake --install build --prefix /your/prefix
# 安装 libbutil.a + butil/ 全部头文件 + config.h
```
