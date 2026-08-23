# brpc 的平台支持情况：Linux only 还是兼容 macOS / FreeBSD / POSIX？

结合 Apache brpc 官方文档、仓库 CI 和源码实现，结论如下：

## 一句话结论

brpc **不是 Linux only**，但 **Linux 是唯一被"完全支持 + 生产环境推荐"的平台**。它基于 **POSIX** 编写（不是 ANSI 纯标准库），macOS 官方支持（性能打折），BSD 系官方不承诺、但源码有兼容处理、可自行编译。

## 官方支持矩阵

| 平台 | 状态 | 说明 |
|---|---|---|
| Linux（Ubuntu/Fedora/CentOS/WSL 等） | 官方支持，生产首选 | 各发行版文档齐全，CI 覆盖 |
| macOS | 官方支持（有 CI 徽章） | 官方明确警告：**相同硬件下性能可能明显差于 Linux**，性能敏感服务不要用 macOS 做生产环境 |
| Windows | 不支持 | 只能走 WSL |
| FreeBSD / 其它 BSD | 官方文档未提及、无 CI | 源码有兼容分支，社区有自行编译成功的案例，但不保证 |

## 源码层面怎么做到"非 Linux 也能跑"

brpc 的核心机制基本都落在 POSIX 接口上，关键平台的差异用 `#ifdef` 分支消化掉了，其中几个关键设计：

1. **I/O 多路复用用的是 `poll(2)` 而不是 `epoll`**（`src/brpc/poll.cpp`）。这是跨平台的关键——`poll` 是 POSIX 标准 API，macOS/BSD 全都支持；brpc 在 poll 之上自研了模拟 epoll 边沿触发的语义（EPOLLET 是用内部状态机模拟的），所以事件驱动核心不依赖 Linux。

1.17.0 已把事件分发重构为 epoll/kqueue 双实现，早期 master 上的 poll.cpp（poll 模拟 epoll）在这个版本已不存在。

2. **bthread 的同步原语（butex）**（`src/bthread/butex.cpp`）：Linux 上用 `futex` 做用户态快速路径；macOS/BSD 上没有 futex，退化为 pthread mutex + condition variable 实现。

3. **线程/CPU 亲和性**：Linux 用 `sched_setaffinity` + `CPU_SET`；macOS 用 `thread_policy_set`，BSD 系也有各自的 `cpuset` 封装。

4. **其它 Linux 私有 API 都有兜底**：`gettid`（macOS 用 `pthread_threadid_np`）、`eventfd`/`timerfd`、`TCP_DEFER_ACCEPT`、`SO_REUSEPORT`（macOS/FreeBSD 本来就支持）等，都有条件编译或替代实现。

## POSIX vs ANSI

- 它是 **POSIX 代码**：用了 pthread、socket、poll、mmap、clock_gettime 等 POSIX API，所以"兼容 POSIX 系统"是它的设计取向，但不是纯 ANSI C/C++（那反而做不了高性能网络框架）。
- 更准确的说法：**"POSIX 优先 + Linux 特化"**——能用 POSIX 的绝不用 Linux 私有 API，必须用 Linux 私有 API 的地方（futex、CPU 亲和性）都留有非 Linux 回退路径。

## 实际建议

- 生产环境：只考虑 Linux。
- 本地开发/调试：macOS 完全可用，但压测数据别拿 macOS 的跟 Linux 比。
- FreeBSD：可以尝试 `cmake` 自行编译，遇到问题大概率要自己修，官方不兜底。
