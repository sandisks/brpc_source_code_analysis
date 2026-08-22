# 1. Overview（概览）

> 来源：https://brpc.apache.org/docs/overview/ （bRPC 官方文档，版本 1.17.0）
> 读者定位：C++ 初学者 + Java 熟手。文中会拿 Java 世界的概念（如 Spring、Netty、gRPC、ExecutorService）做类比。

## 1.1 bRPC 是什么

bRPC（原名 Baidu RPC）是百度开源、现由 Apache 孵化/毕业的**工业级 RPC 框架**，用 C++ 编写。

**RPC**（Remote Procedure Call，远程过程调用）的本质：让你像调用本地函数一样调用另一台机器上的函数。对 Java 熟手来说，它类似：
- gRPC / Dubbo / Spring Cloud OpenFeign 的"能力合集"
- 但 bRPC 在百度/字节/腾讯等公司的超大规模在线服务中久经考验，主打**高吞吐、低延迟、强可观测性**。

bRPC 一句话定位（来自官方）：
- 支持多种协议：**baidu_std**（默认私有协议，类似 gRPC 的 HTTP/2 流式但更轻）、**gRPC（基于 HTTP/2）**、**HTTP**、**Thrift**、**RTMP**、**Redis** 等。
- 一个进程里可以同时用不同协议对外/对内通信。
- 自带三大"神器"：`bvar`（监控指标）、`bthread`（M:N 协程）、`IOBuf`（零拷贝缓冲区）。

## 1.2 为什么工业界 2026 新项目常用 bRPC

| 维度 | 说明 | Java 类比 |
|------|------|-----------|
| 性能 | C++ 无 GC 停顿，P99 延迟稳定 | gRPC-Java 仍有 GC 抖动 |
| 协议广 | 一个框架打通内部私有协议与 gRPC/HTTP | 需 Dubbo + gRPC + OkHttp 拼装 |
| 可观测 | bvar 内置 QPS/延迟/连接数，无需额外埋点 | 需 Micrometer + Prometheus 手动接 |
| 并发 | bthread 轻量级协程，单机轻松百万级 | Netty EventLoop + 业务线程池 |

**版本选择**：截至 2026，Apache 发布的最新稳定版是 **1.17.0（2025-05-28）**。新项目应直接基于 1.17.0 的 API 编写（本仓库的 examples 与笔记均基于此版本）。

## 1.3 核心概念速览（后面章节会逐个展开）

- **Channel（客户端）**：对应 Java 里的 `RestTemplate`/`gRPC Stub` 的"连接入口"。配置协议、超时、负载均衡后，用它发起 RPC。
- **Server（服务端）**：对应 Spring `@RestController` 或 gRPC `Server`。注册"服务"后监听端口。
- **Service / 方法**：用 `.proto` 定义接口，protoc 生成 C++ 代码；你只填业务实现。
- **Controller**：贯穿一次 RPC 的上下文（类似 `HttpServletRequest` + 异常对象），既传错误也传附件。
- **bthread**：bRPC 的"虚拟线程"。你几乎不会直接 `new Thread`，而是 `bthread_start` 或 `std::async` 式的 `bthread`，框架自动 M:N 调度到 pthread。类比 Java 21 的虚拟线程（Project Loom）。

## 1.4 协议对照表

| 协议 | 适用场景 | Java 对应 |
|------|----------|-----------|
| baidu_std | bRPC 内部默认，兼容 gRPC 语义但更省 | 私有协议 |
| grpc | 与 gRPC 生态互通 | gRPC |
| http | 对外 REST / 调试 | Spring MVC |
| redis | 直接当 Redis 客户端 | Jedis/Lettuce |
| thrift | 老系统互通 | Thrift |

> 初学者记住：绝大多数内部服务用 `baidu_std` 或 `grpc`，对外调试用 `http`。

## 1.5 下一步

- 环境搭建 → 见 `getting_started.md`
- 写第一个客户端 → 见 `client_basics.md`
- 写第一个服务端 → 见 `server_basics.md`
- 监控 → `bvar.md`；并发 → `bthread.md`；零拷贝 → `iobuf.md`
