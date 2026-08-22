# 10. Load Balancing（负载均衡）

> 来源：https://brpc.apache.org/docs/rpc-in-depth/load-balancing/ （bRPC 1.17.0）
> 定位：上游如何把流量分给多个下游节点。C++ 初学者向，配 Java 类比。

## 10.1 三个层次

bRPC 的"寻址 + 分发"由三件套组成：

```
NamingService  →  拿到"所有下游节点列表"
LoadBalancer   →  从节点里"挑一个"发请求
HealthCheck    →  把连不上的节点"隔离 + 复活"
```

> Java 熟手类比：
> - `NamingService` ≈ 服务发现（Nacos / Eureka / ZooKeeper 的节点列表）
> - `LoadBalancer` ≈ Ribbon / Spring Cloud LoadBalancer 的策略
> - `HealthCheck` ≈ 注册中心的实例健康探活

## 10.2 NamingService（命名服务）

用 **URL 协议前缀** 一行指定，框架自动解析（1.17 统一三参数 `Init(url, lb_name, options)`）：

```cpp
// 百度命名服务
channel.Init("bns://node-name", "la", &options);
// 从文件读地址列表
channel.Init("file:///path/to/addr.txt", "rr", &options);
// 直接在服务名里列地址（逗号分隔，最常用的本地测试方式）
channel.Init("list://127.0.0.1:8000,127.0.0.1:8001", "la", &options);
// 域名解析（DNS）
channel.Init("http://example.com", "rr", &options);
```

**实现细节（框架内部）**：
- bns：无事件通知，默认每 **5 秒**拉一次最新列表（基于 `PeriodicNamingService`，你只需实现一次怎么取 `GetServers`）。
- file：监听文件修改时间，文件变了重新读。
- list：地址写死在字符串里，取一次就退。
- 新列表通过 `NamingServiceActions::ResetServers` 告诉框架；框架去重、通知观察者（`NamingServiceWatcher`）；整个过程跑在独立 bthread（`NamingServiceThread`）里，可被多个 Channel 共享。

## 10.3 LoadBalancer（负载均衡算法）

bRPC 用**字符串**指代一个负载均衡算法，在 `global.cpp` 里注册（框架已内置常见算法）。常见算法：

| 算法名（字符串） | 含义 | Java 类比 |
|------------------|------|-----------|
| `rr` | Round Robin 轮询 | Ribbon RoundRobin |
| `random` | 随机 | 随机 |
| `la` | Locality-Aware 局部感知（按延迟/负载加权，默认最常用） | 加权最小连接 |
| `c_murmurhash` | 一致性哈希（按 key 取模，同 key 落同节点） | 一致性哈希 |
| `consistent_hashing` | 一致性哈希（带虚拟节点） | 一致性哈希 |

**如何选择**：
- 无状态服务、要均匀 → `rr` 或 `random`。
- **默认推荐 `la`**：它会记录每个节点的延迟和错误率，自动把流量导向"又快又稳"的节点，长尾友好。
- 需要"同用户落到同节点"（如缓存亲和）→ `c_murmurhash` / `consistent_hashing`，配合 `Controller.set_request_code()` 设定 hash key。

```cpp
brpc::ChannelOptions options;
options.timeout_ms = 1000;
// 1.17：负载均衡算法作为 Init 的第 2 个参数
channel.Init("list://127.0.0.1:8000,127.0.0.1:8001", "la", &options);
```

> ⚠️ 1.17 注意：老文档的 `options.load_balancer_name = "la"` 已废弃，统一走 `Init(url, lb_name, options)` 三参数形式。

> 核心难点（框架已解决）：怎么让**不同线程**的负载均衡不互斥？bRPC 用 `DoublyBufferedData` 技术做到读无锁、切换低开销。

## 10.4 HealthCheck（健康检查）

对于还在 NamingService 列表里、但连不上的节点：
- bRPC 定期尝试连接，连上后"复活"该 Socket，重新可被 LoadBalancer 选中。
- 一个被 SetFailed 的 Socket，框架动态起一个 **bthread 专门做健康检查**（`Socket::HealthCheckThread`），其生命周期由该连接管理。
- 关键规则：**在 LoadBalancer 里或做健康检查的节点，一定先在 NamingService 里**。即：只要节点不被删出服务发现，它要么正常（会被选中），要么在做健康检查。

```
节点状态机：
  NamingService 列表
     ├── 正常   → 可被 LoadBalancer 选中
     └── 连不上 → SetFailed → 起健康检查 bthread → 连上 → Revive（复活）
```

## 10.5 例子对应

本仓库 `examples/load_balancing/` 演示 `list://` + `la` 负载均衡，启动两个 server 观察流量分配（日志见根目录 `build_logs/`）。
