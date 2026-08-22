# 8. Combo Channels（组合 Channel）

> 来源：https://brpc.apache.org/docs/client/combo-channels/ （bRPC 1.17.0）
> 定位：把多个子 Channel 组合成一个逻辑 Channel，实现"并行/选择"调用。C++ 初学者向，配 Java 类比。

## 8.1 为什么需要组合 Channel

有时一个业务请求要扇出到多个下游，或要"访问第一个成功的"。手动管理一堆 Channel + bthread 很麻烦。bRPC 提供两种开箱即用的组合：
- **ParallelChannel**：同时发到所有子 Channel，**全部成功才算成功**（扇出+聚合）。
- **SelectiveChannel**：同时发到所有子 Channel，**任意一个成功即返回**（取最快/首答）。

> Java 熟手类比：
> - `ParallelChannel` ≈ `CompletableFuture.allOf(f1, f2, ...)`（等全部）
> - `SelectiveChannel` ≈ `CompletableFuture.anyOf(f1, f2, ...)`（取最先完成）

## 8.2 ParallelChannel（并行全成功）

```cpp
#include <brpc/channel.h>
#include <brpc/combo_channel.h>

brpc::ParallelChannel channel;
brpc::ChannelOptions opt;
opt.timeout_ms = 500;

// 添加两个子 channel（比如不同机房/不同服务）
brpc::Channel* sub1 = new brpc::Channel();
brpc::Channel* sub2 = new brpc::Channel();
sub1->Init("list://10.0.0.1:8000,10.0.0.2:8000", &opt);
sub2->Init("list://10.0.1.1:8000", &opt);

// 第2个参数 true = 失败一个就整体失败；第3个参数用于自定义合并响应
channel.AddChannel(sub1, brpc::ParallelChannelOptions());
channel.AddChannel(sub2, brpc::ParallelChannelOptions());

// 调用：request/response 必须是"数组"风格
// 实际用法较复杂，建议直接参考官方 example/parallel_echo_c++
example::EchoRequest req;
example::EchoResponse res;
brpc::Controller cntl;
req.set_message("parallel");
channel.CallMethod(nullptr, &cntl, &req, &res, nullptr);
```

**要点**：
- 子 Channel 的响应会按顺序拼回。需要"把多个 response 合并成一个"时，要实现合并回调。
- 任一子 Channel 失败 → 整体失败（可配置）。

## 8.3 SelectiveChannel（选最快的）

```cpp
#include <brpc/selective_channel.h>   // 1.17 中独立头文件

brpc::SelectiveChannel channel;
brpc::ChannelOptions sc_opt;          // 1.17 无 SelectiveChannelOptions，直接用 ChannelOptions
sc_opt.timeout_ms = 500;
// 第 1 个参数是负载均衡算法（rr/random 等），把请求分发给各子 channel
channel.Init("rr", &sc_opt);

brpc::Channel* subA = new brpc::Channel();
brpc::Channel* subB = new brpc::Channel();
subA->Init("list://10.0.0.1:8000", "la", &opt);
subB->Init("list://10.0.1.1:8000", "la", &opt);
channel.AddChannel(subA, NULL);       // 第 2 个参数是句柄，可为空
channel.AddChannel(subB, NULL);

example::EchoRequest req;
example::EchoResponse res;
brpc::Controller cntl;
req.set_message("select");
channel.CallMethod(nullptr, &cntl, &req, &res, nullptr);  // 谁先回用谁
```

> ⚠️ 1.17 注意：老文档里的 `SelectiveChannelOptions`、`AddChannel(ch)`、`Init("election", ...)` 均已废弃；子 channel 的 `Init` 也要用三参数形式。

**要点**：
- 适合"要可用性、访问多个等价副本取最快"的场景。
- 与 Backup Request（见 `backup_request.md`）不同：SelectiveChannel 是**同时**发全部；Backup Request 是**先发一个，超时再发第二个**。

## 8.4 和 Backup Request 的区别

| 方式 | 发送时机 | 后端压力 | 适用 |
|------|----------|----------|------|
| SelectiveChannel | 同时发全部 | 高（N 倍） | 副本完全等价、要最快 |
| Backup Request | 先1后2（超时触发） | 低（通常只1次） | 常规降尾延迟 |
| 双异步 + join | 同时发2个 | 高 | 不推荐 |

## 8.5 例子对应

本仓库 `examples/selective_echo/` 演示 SelectiveChannel 用法。
