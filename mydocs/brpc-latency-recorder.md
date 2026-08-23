# brpc LatencyRecorder 源码分析 —— 一行代码的延迟监控是怎么实现的

> 源码位置：`src/bvar/latency_recorder.h` / `latency_recorder.cpp`（brpc 层无独立实现，直接复用 `bvar::LatencyRecorder`）
> 底层机制：`src/bvar/recorder.h`（IntRecorder）、`src/bvar/detail/percentile.h`（Percentile）、`src/bvar/collector.h/.cpp`（采样线程）、`src/bvar/window.h`（滑动窗口）、`src/bvar/cdf.h`（CDF）

## 1. 总体结构：一个 Recorder，11 个内部 bvar

```cpp
// bvar/latency_recorder.h:49  LatencyRecorderBase 的成员
IntRecorder  _latency;                 // 全量延迟（精确：样本数+总和）
Maxer<int64_t> _max_latency;           // 全量最大值（精确）
Percentile   _latency_percentile;      // 采样延迟（近似：P50/P90/P99）
RecorderWindow _latency_window;        // 滑动窗口平均延迟
MaxWindow    _max_latency_window;      // 滑动窗口最大延迟
PercentileWindow _latency_percentile_window;  // 滑动窗口分位数
PassiveStatus<int64_t> _count;         // 总请求数
PassiveStatus<int64_t> _qps;           // 每秒请求数
PassiveStatus<int64_t> _latency_p1;    // 默认 80% 分位
PassiveStatus<int64_t> _latency_p2;    // 默认 90% 分位
PassiveStatus<int64_t> _latency_p3;    // 默认 99% 分位
PassiveStatus<int64_t> _latency_999;   // 99.9% 分位
PassiveStatus<int64_t> _latency_9999;  // 99.99% 分位
CDF          _latency_cdf;             // CDF 曲线（20 个采样点）
PassiveStatus<Vector<int64_t, 4> > _latency_percentiles;  // [p1,p2,p3,99.9]
```

**一次 `recorder << latency_us` 同时做三件事**（数据流）：

```
                     ┌─► _latency（IntRecorder）        → count / 平均 / QPS（精确，全量）
recorder << latency ─┼─► _max_latency（Maxer）          → 最大延迟（精确）
                     └─► _latency_percentile（Percentile）→ P50/P90/P99（近似，采样）
```

## 2. 精确统计：IntRecorder 与滑动窗口

### 2.1 IntRecorder —— 无锁、零浪费的全量计数

`recorder.h` 的 `IntRecorder` 基于 **AgentCombiner**：每个线程一个独立 agent（`thread_local`），`operator<<` 把样本压缩进一个 64 位整数：

```
int64_t pack = ((int64_t)num << 43) | (sign << 42) | sum
               ↑ 20 位样本数    ↑1位符号    ↑43 位总和
```

- 每线程独立累加 → **无锁、无竞争**，高并发下每线程只需改自己的 64 位
- 查询时 `combine_agents()` 把所有线程的 agent 解压合并成 `Stat{sum, num}`
- 平均延迟 = `sum / num`（`get_average_int()`）

### 2.2 滑动窗口 —— Window/Sampler 每 1 秒拍一次快照

`window.h` 的 `Window<R, SERIES_IN_SECOND>`：

- 每个 bvar 自带一个 `Sampler`，后台采样线程（`Sampler::take_sample()`）**每秒**把当前值快照进环形数组
- `LatencyRecorderBase(window_size)`：窗口大小 = 构造参数，传负值时取 `FLAGS_bvar_dump_interval`（**默认 10 秒**）
- 查询 `latency()` = `_latency_window.get_value()`：拿"窗口内首尾两个快照的差"再求平均 → 得到**最近 10 秒的滑动平均延迟**
- `get_span(&s)` 同时返回 `s.data`（窗口内新增样本数）和 `s.time_us`（时间跨度）

**QPS 就是这么算的**（`latency_recorder.cpp` 的 `get_window_recorder_qps`）：

```cpp
qps = s.data.num * 1000000.0 / s.time_us;   // 窗口内样本数 ÷ 窗口时间跨度
// 用 double_to_random_int 做"按期望值随机取整"，防止 1 秒窗口出现周期抖动
```

`count()` 则是全量总次数：`_latency.get_value().num`（不取窗口）。

## 3. 近似统计：P50/P90/P99 的采样链路

百分位**不存全量样本**（内存无法承受），而是走"采样 + 分桶近似"：

### 3.1 采样率控制（collector.h）

```cpp
inline size_t is_collectable(CollectorSpeedLimit* speed_limit) {
    // 16384 基数上的概率采样：fast_rand() & 16383 >= sampling_range 则丢弃
    if ((butil::fast_rand() & (COLLECTOR_SAMPLING_BASE - 1)) >= sampling_range)
        return INVALID_SAMPLING_RANGE;
}
```

- `Collector` 线程按目标速率 **`-bvar_collector_expected_per_second=1000`**（默认每秒 1000 个样本）自适应调整 `sampling_range`（即采样概率）
- 命中采样的样本才 `submit()` 进采样缓冲；`cpuwide_us` 与采集线程时间戳差太远（线程没被调度）的样本直接 `destroy()`，避免内存爆炸

### 3.2 两级样本缓冲（detail/percentile.h）

```cpp
typedef PercentileSamples<254> GlobalPercentileSamples;     // 全局合并后 254 样本
typedef PercentileSamples<30>  ThreadLocalPercentileSamples; // 每线程缓冲 30 样本
```

- `Percentile` 用 `AgentCombiner<Global, ThreadLocal>`：每线程先攒 30 个样本，`merge()` 时**按等概率抽样**并入全局（每个样本等概率保留）
- `PercentileInterval`：循环缓冲 `_samples[SAMPLE_SIZE]`，查询时先 `std::sort` 再取目标下标
- `get_number(ratio)` 求分位数：

```cpp
uint32_t get_number(double ratio) {
    size_t n = (size_t)ceil(ratio * _num_added);   // 要找第 n 大的样本
    // 在 NUM_INTERVALS 个 interval 里按比例定位，interval 内已排序 → 直接取下标
}
```

### 3.3 窗口化分位数

`_latency_percentile_window = Window<Percentile, SERIES_IN_SECOND>`，同样每秒快照、默认 10 秒窗口。`latency_percentile(ratio)` 返回窗口内 `GlobalPercentileSamples::get_number(ratio)`。

**总结采样链路**：

```
每秒 ~N 个样本被 is_collectable() 概率抽中
  → submit() 进 ThreadLocalPercentileSamples(30)
  → Collector 线程周期性批量 merge 进 GlobalPercentileSamples(254)
  → 每秒快照进 PercentileWindow（环形 Series）
  → latency_percentile(ratio) = get_number(ceil(ratio*num))
```

## 4. CDF：一次取 20 个分位点

`CDF`（`bvar/cdf.h`）内部就是 20 个 `Percentile` 采样点的集合（10%→99.99%），`describe_series` 输出 JSON 数组：

```
[{ "ratio": 0.10, "value": 123 }, { "ratio": 0.20, "value": 145 }, ..., { "ratio": 0.9999, "value": 99000 }]
```

监控面板拿到这 20 个点即可画一条平滑的延迟 CDF 曲线。

## 5. 精确 vs 近似 —— 各自代价

| 指标 | 精确性 | 原理 | 代价 |
|---|---|---|---|
| 平均延迟 `latency()` | 精确 | IntRecorder 全量 sum/num | 每线程 64 位，几乎为 0 |
| 最大延迟 `max_latency()` | 精确 | Maxer 全量 | 几乎为 0 |
| 请求数 `count()` | 精确 | IntRecorder num | 几乎为 0 |
| QPS `qps()` | 精确（窗口内） | 窗口快照差值 | 秒级粒度 |
| P50/P90/P99 `latency_percentile()` | **近似** | 概率采样 + 分桶 + 等概率 merge | 内存受限，语义可解释 |

所以：**平均/最大是精确的，分位数是近似的**。这也是为什么 P99 对超大流量足够、但"精确到微秒的 P99"不是该设计的追求。

## 6. 暴露成 bvar：一行代码的魔法

```cpp
bvar::LatencyRecorder write_recorder;
write_recorder.expose("foo_bar_write");
// 自动生成 11 个可监控 bvar：
//   foo_bar_write_latency             平均延迟(us)
//   foo_bar_write_max_latency         最大延迟(us)
//   foo_bar_write_count               请求数
//   foo_bar_write_qps                 QPS
//   foo_bar_write_latency_80/90/99    分位数(us)
//   foo_bar_write_latency_999/9999
//   foo_bar_write_latency_percentiles [80,90,99,99.9]
//   foo_bar_write_latency_cdf         CDF 曲线 20 点
```

`expose()` 内部对每个成员调 `expose_as(prefix, name)`，与任何手写 bvar 一样进入 `bvar` 全局注册表，可通过内置 HTTP 接口 `/vars`、`/brpc` 页面或 dump 文件查询。

## 7. 关键 gflag

| flag | 默认 | 作用 |
|---|---|---|
| `-bvar_collector_expected_per_second` | 1000 | 每秒采集样本目标数（控制分位数精度与内存） |
| `-bvar_collector_max_pending_samples` | 1000 | 待处理样本过多时直接销毁（防内存爆炸） |
| `-bvar_dump_interval` | 10 | 采样快照周期，也是 LatencyRecorder 默认窗口大小（秒） |

## 8. 使用方式（业务代码）

```cpp
#include <bvar/latency_recorder.h>

bvar::LatencyRecorder g_echo_latency;          // 全局单例
g_echo_latency.expose("echo");                 // 暴露 echo_latency 等 11 个变量

// RPC 处理结束时记录一次延迟（微秒）
g_echo_latency << latency_us;                  // operator<< 一行搞定

// 需要"错误数"时另配一个计数（LatencyRecorder 本身不含错误统计）：
bvar::Adder<int64_t> g_echo_error_count;
if (cntl->Failed()) { g_echo_error_count << 1; }
```

**关于"错误数"**：`LatencyRecorder` 只统计延迟/次数/QPS/分位数，**内部没有错误计数器**。错误数通常用 `bvar::Adder<int64_t>` 自行统计，或读 `brpc::Server` 暴露的 `error_code` 系列 bvar（服务端按错误码分桶）。
