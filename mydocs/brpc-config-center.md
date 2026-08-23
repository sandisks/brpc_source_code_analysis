# brpc 为什么不用 nacos 等配置中心？

> 结论：**brpc 不是"不用"配置中心，而是把"配置中心"抽象成了一个可插拔的接口——`NamingService`**，nacos 只是它的一个可选项。

## 1. brpc 的定位：RPC 框架，不是注册中心

brpc 需要解决的是"寻址 + 分发"，它把这拆成三件套：

```
NamingService  →  拿到"所有下游节点列表"
LoadBalancer   →  从节点里"挑一个"发请求
HealthCheck    →  把连不上的节点"隔离 + 复活"
```

配置中心（Nacos/Eureka/ZK）解决的只是**第一行**——提供节点列表。brpc 用 URL scheme 一行指定数据源：

```cpp
channel.Init("bns://node-name", ...);              // 百度 BNS
channel.Init("file:///path/to/addr.txt", ...);     // 本地文件
channel.Init("list://127.0.0.1:8000,8001", ...);   // 内联地址（最常用）
channel.Init("http://example.com", ...);           // DNS 域名
```

`nacos://`、`consul://`、`etcd://` 在社区里都有 NamingService 实现，只是不在官方默认包里。

## 2. 为什么默认不绑定某个配置中心

| 原因 | 说明 |
|---|---|
| **通用性** | 绑定 nacos = 强制所有用户先部署 nacos。brpc 是底层 RPC 库，面向各种规模的部署（单机测试、小集群、超大规模），`list://`/`file://` 零依赖就能跑 |
| **模型不同** | nacos 是"注册中心推送/长轮询"；brpc 内置的 `PeriodicNamingService` 是**定时拉取**（如 BNS 每 5s 拉一次），事件驱动不是必需 |
| **历史原因** | brpc 出身百度，内部生态是 BNS，开源后保持插件化，让生态各自接 etcd/consul/nacos |
| **职责单一** | brpc 自己已有 `LoadBalancer` + 连接级 `HealthCheck`（SetFailed → 健康检查 bthread → Revive），不需要注册中心替它做探活，两者是互补而非替代 |

## 3. 什么场景才该上配置中心

- 多环境（dev/staging/prod）动态切换地址
- 服务频繁扩缩容，不想改代码重发
- 跨语言服务治理（注册中心同时管 Java/Go/C++ 实例）
- 需要"配置管理 + 注册发现"一体化

## 4. 接入成本：实现一个 NamingService 即可

接入 nacos 只需实现一个 `NamingService` 子类（继承 `PeriodicNamingService`，只写 `GetServers` 怎么从 nacos 拉列表，约几十行），然后在 `channel.Init("nacos://...")` 时注册进去。

仓库的 `examples/load_balancing/` 用 `list://` 演示的正是这个抽象：**换成 nacos 只改一行 URL，其余代码不动**。
