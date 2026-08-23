# RDMA 是什么

**RDMA = Remote Direct Memory Access（远程直接内存访问）**：一种让网卡直接读写**远端主机内存**的技术，数据不经过对端 CPU、不经过操作系统内核，实现网络传输的"零拷贝 + 旁路内核"。

## 为什么需要 RDMA：与传统 TCP 的对比

传统 TCP 发一次数据的路径（以发送 100KB 为例）：

```
应用缓冲区 →（拷贝）→ 内核 socket 发送缓冲 →（拷贝）→ 网卡 → 网络
                                                       ↓
接收方：网卡 →（中断/拷贝）→ 内核接收缓冲 →（拷贝）→ 应用缓冲区
```

- 数据在内存间被**多次拷贝**（应用↔内核↔网卡）
- 每次收发都要**经过内核**（系统调用、协议栈处理、中断）
- CPU 是瓶颈，延迟和吞吐都受限

RDMA 的路径：

```
应用缓冲区 ←———————— 网卡直接 DMA 读写 ————————→ 远端应用缓冲区
   （发送方注册内存、把地址告诉网卡，网卡自己把数据读走/放进来）
```

- **零拷贝**：网卡直接读写应用内存，没有内核缓冲，没有 memcpy
- **旁路内核**：不经过协议栈，不进内核（userspace verbs 直接操作网卡）
- **CPU 几乎不参与**：接收方 CPU 甚至不知道数据到了（单边操作时）

## 核心概念（verbs API）

RDMA 编程模型由 InfiniBand verbs API 定义（brpc 用的就是 `<infiniband/verbs.h>`）：

| 概念 | 说明 |
|---|---|
| **HCA** | 主机通道适配器，即 RDMA 网卡 |
| **QP（Queue Pair）** | 一对队列：发送队列 SQ + 接收队列 RQ，通信的最小单元 |
| **CQ（Completion Queue）** | 完成队列：操作完成后放一个 CQE（完成事件），软件从这里拿结果 |
| **MR / lkey / rkey** | 内存注册（`ibv_reg_mr`）：把一块内存注册给网卡，lkey 本地用、rkey 远端用，网卡凭 key 访问内存 |
| **PD（Protection Domain）** | 保护域：把 QP、MR 等资源划在一个安全域内 |
| **WR / WQE** | Work Request：一条"请网卡做 X"的指令（发给 QP 的请求） |
| **GID / LID** | 网卡的寻址标识（LID 是子网内 ID，GID 是全局 ID，类似 IP） |
| **SGE** | 散聚表项：描述"数据在哪块内存、多长"，一个 WR 可带多个 SGE |

## 三种操作类型

| 类型 | 是否需要对方 CPU | 说明 |
|---|---|---|
| **SEND / RECV** | 需要（对方要预先 post RECV） | 类似 TCP 收发，双方都要参与，靠 IMM data 捎带小数据 |
| **RDMA READ** | 不需要 | 主动去**读**远端内存（对方 CPU 无感知） |
| **RDMA WRITE** | 不需要 | 主动去**写**远端内存（对方 CPU 无感知） |
| 原子操作 | 不需要 | 远端的 CAS / FADD |

> 注：所谓"零拷贝"主要指**内存零拷贝**（网卡直接访问应用内存）；数据还是要过网线的。

## 传输语义

- **RC（Reliable Connection）**：可靠、有序、有确认重传（类似 TCP），最常用
- **UC（Unreliable Connection）**：不可靠，无重传
- **UD（Unreliable Datagram）**：不可靠数据报（类似 UDP），支持多对一
- brpc 的 RDMA 实现用的是 **RC**（连接型可靠传输）

## 硬件形态

| 形态 | 说明 |
|---|---|
| **InfiniBand** | 专用网络（专用交换机/线缆），性能最好，成本最高 |
| **RoCE v1/v2** | RDMA over Converged Ethernet：跑在以太网上，成本低，最普及（v2 走 IP/UDP 封装） |
| **iWARP** | RDMA over TCP：把 RDMA 塞进 TCP 栈，兼容性好但性能受限 |

## 应用场景

- HPC 高性能计算（MPI 通信）
- 分布式存储（Ceph、NVMe-oF）
- 分布式 KV / 数据库（需要极低延迟，如 µs 级）
- 机器学习分布式训练（NCCL 用的就是 RDMA）
- 高性能 RPC（brpc 的 RDMA 传输）

---

## brpc 中的 RDMA 实现（结合 1.17.0 源码）

源码位置：`src/brpc/rdma/`，共 3 个文件，整个模块用 `#if BRPC_WITH_RDMA` 包裹（编译开关，无 RDMA 硬件时直接编空实现）。

### 1. `rdma_helper.h/cpp` — 全局初始化与内存注册

```cpp
// 全局初始化（失败即退出）
void GlobalRdmaInitializeOrDie();
// 内存注册：给 IOBuf 里的内存注册 RDMA，返回 lkey（0 表示失败）
uint32_t RegisterMemoryForRdma(void* buf, size_t len);
// 查某地址的 lkey / 全局 PD / 上下文 / GID / LID
uint32_t GetLKey(void* buf);
ibv_pd* GetRdmaPd();
bool IsRdmaAvailable();        // 环境是否可用
bool SupportedByRdma(std::string protocol);  // 某协议是否支持 RDMA
```

功能：打开设备、创建 `ibv_context`、分配 PD、查 GID/LID、计算 CQ 推荐 comp_vector，以及**把任意内存注册成 MR 并返回 lkey**——brpc 要求通过 `append_user_data_with_meta` 把 lkey 作为 meta 挂到 IOBuf 上，这样发 RDMA 时网卡知道去哪块内存取数。

### 2. `block_pool.h/cpp` — RDMA 专用内存池

为什么需要池化（头文件注释写得很清楚）：**RDMA 传输要求数据必须位于"已注册"的内存空间**，所以不能每次从 tcmalloc 现分配现注册（注册代价大），而是：

- 先从 tcmalloc 拿一大块内存（默认 1GB），调一次 `ibv_reg_mr` 整体注册
- 之后所有发送/接收都用这块已注册内存的切片
- 分配单位为 **block**（8KB / 64KB / 2MB 三种规格，按需取最小够用的），配合 IOBuf 的分块机制
- 每块大内存叫一个 **region**（最多 16 个），`GetRegionId()` 拿地址反查 region 得到 lkey

```cpp
bool InitBlockPool(RegisterCallback cb);  // 注册回调 = brpc 的内存注册
void* AllocBlock(size_t size);            // 分配 block
void* ExtendBlockPoolByUser(...);         // 高级用法：用户指定大页等内存区域
uint32_t GetRegionId(const void* buf);    // 地址 → region ID（即 lkey）
```

### 3. `rdma_endpoint.h/cpp` — 连接状态机与收发核心（最大的一坨）

`RdmaEndpoint` 挂在 brpc 的 `Socket` 上（继承 `SocketUser`），把 RDMA 会话接入 brpc 既有框架。要点：

- **握手状态机**（`enum State`）：
  ```
  UNINIT → C_ALLOC_QPCQ → C_HELLO_SEND → C_HELLO_WAIT → C_BRINGUP_QP → C_ACK_SEND → ESTABLISHED
  服务端：S_HELLO_WAIT → S_ALLOC_QPCQ → S_BRINGUP_QP → S_HELLO_SEND → S_ACK_WAIT → ESTABLISHED
  异常：FALLBACK_TCP（降级回 TCP）/ FAILED
  ```
  先建 TCP 连接用于控制面握手（HELLO/ACK），交换 GID、LID、QP 号后 `BringUpQp()` 把 QP 从 RESET 带到 RTS，之后数据走 RDMA。
- **两种运行模式**：
  - **polling 模式**（`-rdma_use_polling`）：起 `rdma_poller_num` 个 bthread 轮询 CQ，`PollCq()` 处理 CQE，延迟更低（注释提到 callback 可为 io_uring/spdk 预留）
  - **event 模式**（默认）：用 `ibv_comp_channel` + `ReqNotifyCq()` 事件通知
- **流量控制（窗口管理）**：通过 IMM 数据里的窗口字段同步 `_sq_window_size` / `_remote_rq_window_size`（远端 RQ 剩余容量），类似 TCP 的滑动窗口，防止 WR 溢出对方 RQ
- **SEND/RECV + IMM 捎带**：`SendImm()` 发 IMM 数据（含窗口信息），`SendAck()` 回 ACK；`HandleCompletion()` 处理 CQE，把收到的数据直接 append 进 IOBuf（零拷贝）
- **安全降级**：握手失败或运行异常 → `FALLBACK_TCP`，透明降级回 TCP，不影响上层 RPC

### 4. 与传输层/协议框架的衔接

- `src/brpc/rdma_transport.h/cpp` + `transport_factory.cpp`：传输层抽象出 TCP / RDMA 两种实现，`transport_factory` 里 `#if BRPC_WITH_RDMA` 注册 RDMA 传输
- `src/brpc/controller.cpp`：`#if BRPC_WITH_RDMA` 提供 `use_rdma` 相关控制器开关
- 编译：`BRPC_WITH_RDMA=ON` 开启（需要 infiniband 头文件和 RoCE/IB 网卡），默认关闭

## 优缺点总结

| | 优点 | 缺点 |
|---|---|---|
| 延迟 | 亚微秒级（远低于 TCP 的几十微秒） | 需要专用硬件（RoCE 网卡） |
| 吞吐 | 高（网卡直连内存，CPU 不兜底） | 部署复杂（子网/路由/PFC 配置） |
| CPU | 基本不占 | 内存必须注册、有池化/对齐限制 |
| 生态 | HPC/存储/分布式标配 | 协议栈较薄，错误处理要自己兜 |

一句话：**RDMA 让网卡直接"摸"远端内存，绕开内核和 CPU，换来的是极低延迟和零拷贝；brpc 把 RDMA 封装成和 TCP 平级的传输层，握手走 TCP 控制面、数据面走 RC QP，失败自动降级回 TCP。**
