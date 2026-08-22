# 5. bvar（监控指标）

> 来源：https://brpc.apache.org/docs/bvar/bvar-c++/ （bRPC 1.17.0）
> 定位：bRPC 内置的高性能计数器库。C++ 初学者向，配 Java 类比。

## 5.1 为什么需要 bvar

工业服务必须能回答："QPS 多少？P99 延迟多少？错误率多少？" 如果每次加计数都加锁，高并发下会成为瓶颈。

bvar 的设计哲学：**写少读多、几乎无锁**。计数器更新走 thread-local，读取时才汇总。性能开销极低（纳秒级）。

> Java 熟手类比：类似 `java.util.concurrent.atomic.LongAdder`（分段计数减少竞争）+ Micrometer 的指标模型，但 bvar 直接用 C++ 模板实现，零 GC。

## 5.2 访问你自己的 bvar

bRPC 启动 Server 后，自动暴露 `/vars` 接口（HTTP）。浏览器或 curl 打开即可看到所有指标：

```bash
curl http://127.0.0.1:8000/vars
```

也内置 `/flags`（运行时 flag）、`/status`（进程状态）、`/rpcz`（RPC 链路追踪）。

## 5.3 最常用的几种 bvar

### (1) Adder —— 累加器（最常用）

```cpp
#include <bvar/bvar.h>

bvar::Adder<int> g_request_count("my_service", "request_count");
// 业务里：
g_request_count << 1;        // 累加 1
// 或 g_request_count.increment();

// 读取当前值：
int64_t n = g_request_count.get_value();
```

> Java 熟手类比：`LongAdder`。`<<` 是 bvar 自定义的累加运算符，等价于 `increment()`。

### (2) Window —— 滑动窗口（最近 N 秒的聚合）

```cpp
// 统计"最近 60 秒内"的累加值
bvar::Window<bvar::Adder<int>> g_req_count_min(
    &g_request_count, 60);
// g_req_count_min.get_value() 就是 60 秒窗口内的总请求数
```

### (3) PerSecond —— 每秒速率（QPS）

```cpp
bvar::PerSecond<bvar::Adder<int>> g_qps(
    &g_request_count, 1);   // 第2个参数=窗口秒数
// g_qps.get_value() ≈ 当前 QPS
```

### (4) LatencyRecorder —— 延迟记录器（强烈推荐）

一行代码获得完整延迟统计（平均、P50/P90/P99、QPS、错误数、CDF）：

```cpp
bvar::LatencyRecorder my_func_latency("my_func");

// 业务里记录耗时（单位：微秒）
butil::Timer tm;
tm.start();
my_func();
tm.stop();
my_func_latency << tm.u_elapsed();   // u=微秒；也可用 s/m/n_elapsed
```

暴露出的指标（自动加前缀 `my_func_`）：
- `my_func_qps`
- `my_func_latency` （平均）
- `my_func_latency_percentile` （P50/90/99/999）
- `my_func_latency_cdf` （累积分布，用于选 backup_request_ms）

## 5.4 命名约定

`bvar::Adder<int> x("my_service", "request_count");`
- 第一个参数是**模块名（namespace）**，第二个是**变量名**。
- 在 `/vars` 里显示为 `my_service_request_count`（中间用 `_` 连接）。
- 同名同名变量要唯一，否则 bvar 会 assert。

## 5.5 例子对应

本仓库 `examples/bvar_demo/` 演示了 Adder + LatencyRecorder 并用 `/vars` 查看。
