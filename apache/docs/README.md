# bRPC 1.17.0 官方文档学习笔记

> 依据 Apache bRPC 官网（https://brpc.apache.org/docs/）文档整理。
> 面向读者：**C++ 初学者 + Java 熟手**（笔记中大量对照 Java 概念）。
> 版本：**1.17.0**（2025-05-28 发布，2026 年工业界新项目的常用稳定版）。
> 配套可编译例子：仓库根目录 `examples/`（CMake 组织，见 `CMakeLists.txt`）。

## 笔记目录

| # | 笔记 | 官网来源 | 对应例子 | 知识点 |
|---|------|----------|----------|--------|
| 1 | [overview.md](overview.md) | /docs/overview/ | — | 什么是 RPC、bRPC 定位、核心概念 |
| 2 | [getting_started.md](getting_started.md) | /docs/getting_started/ | — | 编译安装、依赖、最小程序结构 |
| 3 | [client_basics.md](client_basics.md) | /docs/client/basics/ | echo_client | Channel、同步/异步调用、Controller |
| 4 | [server_basics.md](server_basics.md) | /docs/server/basics/ | echo_server | proto、Service 实现、Server 启动 |
| 5 | [bvar.md](bvar.md) | /docs/bvar/bvar-c++/ | bvar_demo | 指标监控：Adder/Window/PerSecond/LatencyRecorder |
| 6 | [bthread.md](bthread.md) | /docs/bthread/bthread/ | bthread_demo | M:N 协程、bthread 同步原语 |
| 7 | [iobuf.md](iobuf.md) | /docs/c++-base/iobuf/ | iobuf_demo | 零拷贝缓冲区、附件 |
| 8 | [combo_channels.md](combo_channels.md) | /docs/client/combo-channels/ | selective_echo | ParallelChannel / SelectiveChannel |
| 9 | [backup_request.md](backup_request.md) | /docs/client/backup-request/ | backup_request | 备份请求降长尾 |
| 10 | [load_balancing.md](load_balancing.md) | /docs/rpc-in-depth/load-balancing/ | load_balancing | 命名服务、LB 算法、健康检查 |

## 阅读建议（学习路径）

1. 先读 `overview.md` 建立整体认知；
2. 按 `getting_started.md` 用 `scripts/build_brpc.sh` 一键搭好环境；
3. 依次读 3→4（客户端/服务端基础），跑通 `echo_server` + `echo_client`；
4. 再读 5/6/7（bvar/bthread/iobuf），看对应 demo 的输出；
5. 最后读 8/9/10（进阶：组合 Channel、备份请求、负载均衡）。

## 与 1.17.0 版本相关的 API 注意事项

（旧文档/老博客的写法在 1.17 已变化，笔记中已标注 ⚠️）

- `Channel::Init` 统一为三参数：`Init(url, load_balancer_name, options)`；`Init(url, options)` 已废弃。
- 负载均衡算法名（`rr`/`random`/`la`/`c_murmurhash`/`consistent_hashing`）作为 `Init` 第 2 个参数传入，不再是 `options.load_balancer_name`。
- `.proto` 必须加 `option cc_generic_services = true;`，否则 protoc 3.x 不生成 Service/Stub 类。
- `SelectiveChannel` 在独立头文件 `<brpc/selective_channel.h>`；无 `SelectiveChannelOptions`，直接用 `ChannelOptions`；`AddChannel(ch, NULL)`；`Init("rr", &opt)`。
- 异步回调用 `brpc::NewCallback`（`<brpc/callback.h>`），不要用 protobuf 的 `google::protobuf::NewCallback`（protobuf 3.21 已移除）。
