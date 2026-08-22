# 3. Client Basics（客户端基础）

> 来源：https://brpc.apache.org/docs/client/basics/ （bRPC 1.17.0）
> 定位：客户端三种调用方式：同步 / 异步 / 批量。C++ 初学者向，配 Java 类比。

## 3.1 Channel：客户端的"总入口"

`Channel` 类似 Java 的 `RestTemplate` 或 gRPC 的 `ManagedChannel`。

```cpp
#include <brpc/channel.h>

brpc::Channel channel;
brpc::ChannelOptions options;
options.protocol = "baidu_std";          // 协议
options.timeout_ms = 1000;               // 单次 RPC 超时
options.max_retry = 3;                   // 失败重试次数
// 连接单个服务地址（1.17 起统一用三参数 Init）：
channel.Init("127.0.0.1:8000", "", &options);
// 或用命名服务（集群）：list://ip1:port,ip2:port，lb 名 rr/la/random
// channel.Init("list://127.0.0.1:8000,127.0.0.1:8001", "la", &options);
```

> Java 熟手：`ChannelOptions` ≈ gRPC 的 `ManagedChannelBuilder`，但 bRPC 把"命名服务 + 负载均衡"也塞进了这一层（`list://`、`bns://` 等协议前缀 + `Init` 第 2 个参数的 lb 名）。
>
> ⚠️ 1.17 注意：`Init(url, options)` 两参数形式已被弃用（`list://` 会报错），统一用 `Init(url, load_balancer_name, options)`。

## 3.2 同步调用（Blocking）

最常用的方式，逻辑最直观，调用线程会**阻塞**直到返回。

```cpp
brpc::Controller cntl;
example::EchoRequest req;
example::EchoResponse res;
req.set_message("hello");
// Stub 由 protoc 生成，类似 gRPC 的 blockingStub
example::EchoService_Stub stub(&channel);
stub.Echo(&cntl, &req, &res, nullptr);   // 最后一个参数 nullptr = 同步
if (cntl.Failed()) {
    LOG(ERROR) << "RPC failed: " << cntl.ErrorText();
} else {
    LOG(INFO) << "got: " << res.message();
}
```

**要点**：
- `Controller`（常缩写 `cntl`）是贯穿一次 RPC 的上下文对象：传错误、传附件、传 trace 信息。类比 HTTP 的 `HttpServletRequest`/异常对象合体。
- 同步调用底层其实也是基于 bthread 的，阻塞的只是**当前 bthread**，不会卡住 pthread（这是 bRPC 的核心优势）。

> Java 熟手类比：`stub.Echo(..., nullptr)` ≈ gRPC `blockingStub.echo(req)`；`cntl.Failed()` ≈ 抛异常或 `status.isOk()`。

## 3.3 异步调用（Asynchronous）

不阻塞调用线程，回调里拿结果。适合高并发、要把多个 RPC 并发起来的场景。

```cpp
void OnEchoDone(brpc::Controller* cntl,
                example::EchoRequest* req,
                example::EchoResponse* res,
                google::protobuf::Closure* done) {
    // 必须：done->Run() 释放资源（否则内存泄漏！）
    std::unique_ptr<brpc::Controller> cntl_guard(cntl);
    std::unique_ptr<example::EchoRequest> req_guard(req);
    std::unique_ptr<example::EchoResponse> res_guard(res);
    if (cntl->Failed()) {
        LOG(ERROR) << "async RPC failed: " << cntl->ErrorText();
    } else {
        LOG(INFO) << "async got: " << res->message();
    }
    done->Run();   // 告诉框架"我处理完了"
}

// 发起异步调用
example::EchoRequest* req = new example::EchoRequest;
example::EchoResponse* res = new example::EchoResponse;
brpc::Controller* cntl = new brpc::Controller;
req->set_message("hi");
example::EchoService_Stub stub(&channel);
stub.Echo(cntl, req, res, google::protobuf::NewCallback(
    &OnEchoDone, cntl, req, res, done));
```

**要点**：
- 第 4 个参数传一个 `Closure`（回调），就变成异步。
- 异步模式下 `req/res/cntl` 由**你分配、你释放**。`done->Run()` 之前必须还在；回调里用 `unique_ptr` 包起来自动释放。
- `NewCallback` 是 bRPC/protobuf 的辅助宏，把函数指针 + 参数绑定成一个 `Closure`。

> Java 熟手类比：`stub.Echo(cntl, req, res, done)` ≈ gRPC `futureStub.echo(req)` 返回 `ListenableFuture`；`done->Run()` ≈ `future.addListener(...)` 的收尾。`Closure` ≈ `Runnable`。

## 3.4 批量调用（Batch / 并发 Join）

同时发 N 个 RPC，等全部完成再继续。官方推荐用 `CallMany` 或 `ParallelChannel`（见 `combo_channels.md`）。最简单的"等全部"写法：

```cpp
// 用 bthread + bthread_join 并发 N 个同步调用
bthread_t tids[N];
for (int i = 0; i < N; ++i) {
    bthread_start_background(&tids[i], nullptr, DoOneRpc, (void*)&args[i]);
}
for (int i = 0; i < N; ++i) {
    bthread_join(tids[i], nullptr);   // 等全部完成
}
```

> Java 熟手类比：`bthread_start_background` ≈ `executor.submit(task)`；`bthread_join` ≈ `future.get()` 或 `CountDownLatch.await()`。

## 3.5 Controller 常用字段（速查）

| 字段/方法 | 含义 |
|-----------|------|
| `cntl.set_timeout_ms(ms)` | 本次调用单独超时 |
| `cntl.Failed()` | 是否失败 |
| `cntl.ErrorText()` | 错误文本 |
| `cntl.set_log_id(id)` | 串联日志的 log_id（类似 traceId） |
| `cntl.request_attachment()` | 二进制附件（绕过 proto，省序列化） |

## 3.6 例子对应

本仓库 `examples/echo_client/` 演示了同步 + 异步两种调用。运行后日志见根目录 `build_logs/`。
