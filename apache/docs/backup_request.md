# 9. Backup Request（备份请求）

> 来源：https://brpc.apache.org/docs/client/backup-request/ （bRPC 1.17.0）
> 定位：用"超时再发一份"降低长尾延迟。C++ 初学者向，配 Java 类比。

## 9.1 问题背景

线上常遇到：99% 请求 2ms 返回，但 1% 卡 100ms。这种**长尾延迟**很讨厌。原因可能是 GC、锁竞争、网络抖动。

朴素解法：同时发两个请求（双副本），谁先回用谁。但这样**每次都双倍压力**，不划算。

**Backup Request**：正常情况下只发 1 个；超过 `backup_request_ms` 还没回，才发第 2 个。因为只在慢请求上多花一份，整体压力几乎不变。

> Java 熟手类比：类似 Resilience4j / Hystrix 的"对冲请求（hedging）"，或 gRPC 的 hedging policy。

## 9.2 用法一：Channel 直接开 Backup Request

```cpp
brpc::ChannelOptions options;
options.protocol = "baidu_std";
options.timeout_ms = 100;
options.backup_request_ms = 10;   // 关键：10ms 内没回就发备份
// 1.17 三参数 Init：url + 负载均衡算法 + options
channel.Init("list://127.0.0.1:8000,127.0.0.1:8001", "la", &options);
```

行为：
- 先发到某个 server。
- 过了 `backup_request_ms`（10ms）还没回 → 自动发到第 2 个 server。
- 取**先回来**的响应。
- 通常只有 ~1% 的慢请求才会真正发出第 2 份 → 压力几乎不变。

> 官方例子 `example/backup_request_c++`：client 设 `backup_request_ms=2`，server 在请求数为偶数时故意 sleep 20ms，从而触发备份请求。

## 9.3 如何选 backup_request_ms

用 bvar 的 **CDF（累积分布）图**来选：
- y 轴是延迟（默认微秒），x 轴是"延迟小于该值的请求占比"。
- 若选 `backup_request_ms=2ms` 能覆盖 95.5% 的请求 → 剩下 4.5% 走备份。
- 若选 `10ms` 能覆盖 99.99% → 只有 0.01% 走备份。

自己埋点生成 CDF：

```cpp
bvar::LatencyRecorder my_func_latency("my_func");
butil::Timer tm;
tm.start();
my_func();
tm.stop();
my_func_latency << tm.u_elapsed();   // 自动产出 my_func_latency_cdf
```

## 9.4 用法二：无法用命名服务时——SelectiveChannel 上的 Backup Request

```cpp
// 定义带 backup_request_ms 的 SelectiveChannel，内含两个子 channel
// 流程同普通 SelectiveChannel，但先访问一个子 channel，
// 超时后再访问另一个子 channel（可对应两个集群）
// 详见 example/selective_echo_c++
```

- 若子 channel 对应一个集群，则这是"集群间备份"。
- 【不推荐】"发两个异步 RPC 再互相 cancel" 的做法：因为总会发 2 份请求，压力翻倍，不经济（见 `example/cancel_c++`，仅作反面教材）。

## 9.5 与 /rpcz 联动

开启 backup request 后，client 的 `/rpcz` 页面会显示"在 2ms 触发了 backup request 并发送了第二个请求"，便于排查。

## 9.6 例子对应

本仓库 `examples/backup_request/` 演示 `backup_request_ms` 配置与触发效果（日志会显示索引号与延迟）。
