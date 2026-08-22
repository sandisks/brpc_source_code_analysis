# 2. Getting Started（上手）

> 来源：https://brpc.apache.org/docs/getting_started/ （bRPC 1.17.0）
> 定位：在 Linux 上编译并跑通第一个 bRPC 程序。面向 C++ 初学者，附带 Java 类比。

## 2.1 依赖一览

bRPC 是 C++ 项目，需要提前装好：

| 依赖 | 作用 | 安装（Ubuntu/Debian） |
|------|------|----------------------|
| g++ / clang | C++17 编译器 | `apt install g++` |
| CMake >= 3.10 | 构建系统（类比 Maven/Gradle） | `apt install cmake` |
| protobuf (>=3.x) | IDL 与序列化（类比 Protobuf 在 Java 里） | `apt install libprotobuf-dev protobuf-compiler` |
| openssl | 加密/校验 | `apt install libssl-dev` |
| gflags | 命令行参数（类比 Spring 的 `@Value`/args） | `apt install libgflags-dev` |
| leveldb | 可选，rpcz 用 | `apt install libleveldb-dev` |

> Java 熟手类比：protobuf 就是你们用的同一个 `.proto` 体系；gflags 类似 main 函数的 `--key=value` 参数解析，但 bRPC 框架会**自动注册大量内置 flag**（如 `-port`、`-idle_timeout_second`）。

## 2.2 编译 bRPC 源码（方式一：源码构建）

```bash
git clone https://github.com/apache/brpc.git
cd brpc && git checkout 1.17.0      # 锁定 2026 工业界常用版本
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install                   # 安装到 /usr/local
```

编译产物：`libbrpc.a` / `libbrpc.so`。

## 2.3 直接用包管理器（方式二，推荐新手）

```bash
# Ubuntu 24.04 起可用 apt 直接装（版本号随发行版）
apt install libbrpc-dev             # 含头文件与静态库
```

> 注意：发行版包的版本可能滞后，确认 `brpc/version.h` 里的 `BRPC_VERSION` 是否 >= 1.17.0。本仓库 examples 默认链接系统安装的 brpc。

## 2.4 最小程序结构

一个 bRPC 程序通常分三步：

1. **定义接口**：写 `.proto`，用 `protoc` 生成 `xxx.pb.h / .pb.cc`（数据结构）和 `xxx.pb_service.h`（服务骨架）。
2. **实现服务端**：继承生成的 `Service` 类，重写方法；`Server.AddService` 注册；`Server.Start(port)` 启动。
3. **实现客户端**：构造 `Channel`，`Init` 连接；用生成的 `Stub` 发起调用。

> Java 熟手类比：`.proto` + `protoc` 就是你们 `protobuf-maven-plugin` 干的事；`Stub` 就是 gRPC 的 `BlockingStub` / `FutureStub`。

## 2.5 验证安装是否成功

```bash
# 跑官方 example 里的 echo server
cd brpc/build/output/bin
./echo_server &
./echo_client
```

看到 client 打印出 `Received response` 即成功。本仓库 `examples/` 下提供了等价的最小例子（见各知识点对应目录）。

## 2.6 常见坑（初学者）

- **proto 版本**：bRPC 1.17.0 用 protobuf 3，**不要用 proto2 的 `required`**（已废弃）。
- **线程模型**：bRPC 启动后由框架接管 bthread，你的 `main` 函数里要用 `server.RunUntilAskedToQuit()` 或 `sleep` 阻塞，否则进程直接退出（类似 Java 主线程结束 JVM 退出）。
- **链接顺序**：链接时 `-lbrpc -lprotobuf -lgflags -lssl -lcrypto` 顺序不当会报 undefined reference，CMake 里用 `target_link_libraries` 让 CMake 自动处理。
