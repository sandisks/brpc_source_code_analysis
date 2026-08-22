# 4. Server Basics（服务端基础）

> 来源：https://brpc.apache.org/docs/server/basics/ （bRPC 1.17.0）
> 定位：写一个 bRPC 服务端。C++ 初学者向，配 Java 类比。

## 4.1 整体流程

```
.proto 定义 service
   ↓ protoc
生成 EchoService (抽象类) + EchoService_Stub
   ↓
继承 EchoService，实现 Echo() 方法   ← 你的业务逻辑
   ↓
Server.AddService(&impl)              ← 注册
Server.Start(port)                    ← 监听
server.RunUntilAskedToQuit()          ← 阻塞主线程
```

> Java 熟手类比：`Service` 接口 ≈ gRPC 自动生成的 `ImplBase`；`AddService` ≈ 把 Bean 注册进 Spring Context；`RunUntilAskedToQuit` ≈ 维持进程存活的 `CountDownLatch`。

## 4.2 第一步：定义接口（.proto）

```proto
syntax = "proto3";

// 必须！否则 protoc 3.x 默认不生成 Service/Stub 类
option cc_generic_services = true;

package example;

message EchoRequest  { string message = 1; }
message EchoResponse { string message = 1; }

service EchoService {
    rpc Echo(EchoRequest) returns (EchoResponse);
}
```

## 4.3 第二步：实现服务类

```cpp
#include "echo.pb.h"
#include <brpc/server.h>

class EchoServiceImpl : public example::EchoService {
public:
    EchoServiceImpl() {}
    ~EchoServiceImpl() {}

    void Echo(google::protobuf::RpcController* controller,
              const example::EchoRequest* request,
              example::EchoResponse* response,
              google::protobuf::Closure* done) override {
        // 收尾闭包：无论成功失败都要 Run，否则框架一直等
        brpc::ClosureGuard done_guard(done);

        // 业务逻辑：原样返回
        response->set_message(request->message());

        // controller 可拿上下文（log_id、附件等）
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(controller);
        LOG(INFO) << "recv: " << request->message()
                  << ", log_id=" << cntl->log_id();
    }
};
```

**要点**：
- `ClosureGuard done_guard(done)` 是 RAII 写法：构造时绑定 `done`，析构时自动 `done->Run()`。类比 Java 的 `try-finally` 或 `finally { done.run() }`。**忘了 Run 会让调用方永远阻塞。**
- `controller` 要 `static_cast` 成 `brpc::Controller*` 才能用 bRPC 专属字段（log_id、attachment）。
- 服务端方法是**在 bthread 里被调用的**，可以安全地做阻塞（如访问下游），框架会调度。

## 4.4 第三步：启动 Server

```cpp
int main(int argc, char* argv[]) {
    // gflags 自动解析命令行参数（如 --port=8000）
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    brpc::Server server;
    EchoServiceImpl echo_service;

    // 注册服务，OWN 表示 server 拥有该对象生命周期
    if (server.AddService(&echo_service,
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }

    // 启动：监听 0.0.0.0:8000，使用单端口多协议
    brpc::ServerOptions options;
    if (server.Start(8000, &options) != 0) {
        LOG(ERROR) << "Fail to start server";
        return -1;
    }

    // 阻塞主线程，直到收到退出信号（Ctrl-C）
    server.RunUntilAskedToQuit();
    return 0;
}
```

**要点**：
- `ServerOptions` 里可设线程数、协议白名单（`options.enabled_protocols`）、idle 超时等。
- `AddService` 第二个参数：`SERVER_OWNS_SERVICE`（server 析构时 delete）或 `SERVER_DOESNT_OWN_SERVICE`（你自己管）。
- 一个 Server 可同时挂载多个 Service，且**默认单端口支持多协议**（baidu_std / http / grpc 都能打进来）。

## 4.5 一个端口同时提供 HTTP？

把协议设成 `http` 即可用浏览器/curl 调试，无需改业务代码：

```bash
./echo_server --enabled_protocols=http
curl "http://127.0.0.1:8000/example.EchoService/Echo?message=hi"
```

> Java 熟手类比：相当于同一个 `@RestController` 既能被 gRPC 调又能被 HTTP 调，bRPC 自动做协议适配。

## 4.6 例子对应

本仓库 `examples/echo_server/` 是该流程的完整可编译实现。与 `examples/echo_client/` 配套运行。
