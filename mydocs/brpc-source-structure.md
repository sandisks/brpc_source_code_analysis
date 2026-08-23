# brpc 1.17.0 源码结构分析：功能大类与小类

本地源码：`/tmp/brpc_src/brpc-1.17.0`，核心代码全部在 `src/` 下，共 892 个文件（518 .h / 218 .cpp / 99 .cc / 19 .proto）。

## 总览：五个核心库 + 外围

```
src/
├── butil/    基础工具库（最底层，被所有模块依赖；大量从 Chromium/LevelDB 移植）
├── bthread/  M:N 用户态线程库（协程调度，brpc 高性能的核心）
├── bvar/     监控指标采集库（统计、分位数、滑窗）
├── brpc/     RPC 框架本体（协议、服务、客户端、负载均衡……）
├── json2pb/   JSON <-> protobuf 互转
└── mcpack2pb/ mcpack 二进制 <-> protobuf 互转
```

外围：`example/`（示例）、`test/`（测试）、`tools/`（压测/调试工具）、`java/`+`python/`（多语言客户端）、`docs/`（文档）、`bazel/`+`cmake/`+`build-sys/`（构建）。

---

## 1. butil/ — 基础工具库（"百度的 LevelDB/Chromium 底座"）

### 子文件夹
| 目录 | 功能 |
|---|---|
| `containers/` | 容器：`flat_map`（高性能 map）、`bounded_queue`（有界队列）、`mpsc_queue`（多生产者单消费者无锁队列）、`doubly_buffered_data`（双缓冲数据，LB 列表用）、`case_ignored_flat_map` |
| `strings/` | 字符串：`string_printf`、`string_split`、`string16`、UTF-8/16 转换、`safe_sprintf`（Chromium 移植为主） |
| `threading/` | 线程：`platform_thread`（Linux/macOS/FreeBSD 跨平台封装）、`simple_thread`、`thread_local_storage`、`watchdog`（看门狗线程） |
| `memory/` | 内存：`object_pool`、`singleton`、`ref_counted`、`aligned_memory` |
| `time/` | 时间：`clock`、`timer`、`time_delta`、时间格式化（Chromium 移植） |
| `files/` | 文件：`file_util`（posix/linux/mac 三套实现）、`file_enumerator`、`temp_file`、`scoped_temp_dir` |
| `synchronization/` | 同步原语：`atomic_ref_count`、`condition_variable`、`lock`、`waitable_event`（Chromium 移植） |
| `debug/` | 调试：`leak_checker`（内存泄漏检测）、`fault_injection`（故障注入）、`stacktrace` |
| `numerics/` | 数值安全运算（Chromium `safe_math` 移植） |
| `posix/` | POSIX 兼容层（跨平台适配） |
| `mac/` | macOS 专用代码 |
| `third_party/` | 内嵌第三方库：`modp_b64`（base64）、zlib 相关、Chromium 源码片段 |
| `details/` | 内部实现细节 |

### 顶层关键文件（无子目录的部分）
- `iobuf.h/cpp` — **IO 缓冲**，brpc 最核心的零拷贝缓冲结构（`butil::IOBuf`）
- `endpoint.h` — `butil::EndPoint`（ip:port 封装）
- `object_pool.h` / `resource_pool.h` / `arena.h` — 三级内存池（对象池/资源池/竞技场）
- `atomicops.h` + `atomicops_internals_*.h` — 原子操作（x86/arm/riscv/loongarch/mips 多架构）
- `logging.h` / `errno.h` / `status.h` — 日志与错误
- `fast_rand.h` — 快速随机数；`hash.h` — 哈希（murmur/farmhash）；`crc32c.h` — 校验和
- `thread_local.h`、`thread_key.h` — 线程局部存储
- `fd_utility.h`、`fd_guard.h`、`unix_socket.h` — fd 工具
- `base64.h`、`big_endian.h`、`recordio.h`、`process_util.h`、`reloadable_flags.h`

---

## 2. bthread/ — M:N 用户态线程库（协程调度）

bthread 是 brpc 高性能的根基：N 个 pthread 上调度海量 bthread 协程。

### 调度核心
| 文件 | 功能 |
|---|---|
| `task_control.h/cpp` | **全局调度总控**：跨 Worker 调度、空闲线程管理、`bthread_start_background` 入口 |
| `task_group.h/cpp` | **每个 Worker 线程的本地调度**：rq 队列操作、上下文切换时机、steal 入口 |
| `work_stealing_queue.h` | 无锁工作窃取队列（每个 Worker 的本地任务队列） |
| `task_meta.h` | 任务元数据（bthread 状态、栈指针、局部存储等） |
| `context.h/cpp` | **上下文切换**（汇编实现，x86_64/arm 等架构各自版本） |
| `stack.h/cpp` | 栈管理：栈类型（主栈/次级栈）、栈池分配与回收、`get_stack` |
| `processor.h` | CPU 相关：缓存行大小、内存屏障 |

### 同步原语（bthread 内使用，不阻塞 pthread）
| 文件 | 功能 |
|---|---|
| `butex.h/cpp` | **butex 同步原语**：bthread 的 wait/notify，支持超时，底层挂 futex |
| `sys_futex.h/cpp` | futex 封装：Linux 用真 futex，非 Linux 用 `SimuFutex`（pthread mutex+cond 模拟） |
| `mutex.h/cpp` | bthread 互斥锁（`bthread_mutex_t`） |
| `rwlock.h/cpp` | 读写锁 |
| `condition_variable.h/cpp` | 条件变量 |
| `countdown_event.h/cpp` | 倒计时事件（等 N 个事件完成） |
| `semaphore.h/cpp` | 信号量 |
| `parking_lot.h` | "停车位"：线程挂起/唤醒原语（跨平台） |
| `execution_queue.h/cpp` | **执行队列**：任务排队串行执行，bthread 化（异步日志等用它） |
| `timer_thread.h/cpp` | **TimerThread**：定时器线程（延迟任务） |

### 其它
| 文件 | 功能 |
|---|---|
| `id.h/cpp` | ABA-free 的 ID 分配器（`butil::ResourceId` 风格） |
| `list_of_abafree_id.h` | ABA-free ID 列表 |
| `key.h/cpp` | bthread 局部存储 key 管理 |
| `bthread_once.h/cpp` | bthread 安全的一次性初始化 |
| `fd.h/cpp` | fd 的 bthread 封装（把阻塞 fd 操作接入调度器） |
| `interrupt_pthread.h/cpp` | 中断 pthread（配合 bthread 调度） |
| `errno.h/cpp` | bthread 错误码 |
| `task_tracer.h/cpp` | 任务追踪（采样日志） |
| `types.h` | 基础类型定义 |
| `bthread.h` | 对外统一 API 头 |

---

## 3. bvar/ — 监控指标库

### 变量类型
| 文件 | 功能 |
|---|---|
| `variable.h/cpp` | **变量基类**：注册、导出、dump、window 关联 |
| `reducer.h` | 归约变量（`bvar::Adder`、`Maxer`、`Miner` 等：加法/最大/最小） |
| `status.h` | 状态变量（`bvar::Status`，设置任意值） |
| `passive_status.h` | 被动状态（值由回调实时拉取） |
| `latency_recorder.h/cpp` | **延迟记录器**（`LatencyRecorder`，结合分位数统计） |
| `recorder.h` | 采样记录器（维护分位数桶，percentile 计算） |
| `window.h` | **滑窗**：1s/10s/60s 时间窗口统计 |
| `vector.h` | 向量变量（一组值） |
| `mvariable.h/cpp` | 多维变量 |
| `multi_dimension.h` | 多维统计（维度组合） |

### 框架与工具
| 文件 | 功能 |
|---|---|
| `collector.h/cpp` | 采样收集器（被 dump/window 驱动） |
| `gflag.h/cpp` | 把 gflags 配置项暴露为 bvar |
| `scoped_timer.h` | 计时辅助 |
| `default_variables.cpp` | 内置默认变量（进程启动自动注册） |
| `detail/` | 内部实现：`combiner`（归并）、`sampler`（采样）、`percentile`（分位数） |
| `utils/` | 工具函数 |

---

## 4. brpc/ — RPC 框架本体（最大的一块）

### 4.1 顶层：核心 RPC 骨架
| 文件 | 功能 |
|---|---|
| `channel.h/cpp` | **客户端通道**：`brpc::Channel`，发起 RPC 的入口（初始化、CallMethod、销毁） |
| `server.h/cpp` | **服务端**：`brpc::Server`（端口监听、服务注册、请求分发、优雅退出） |
| `controller.h/cpp` | **控制上下文**：`brpc::Controller`（请求参数/响应结果/超时/错误码） |
| `socket.h/cpp` | **Socket 封装**：连接管理、读写缓冲、TCP/RDMA 统一接口 |
| `socket_map.h/cpp` | Socket 池/映射（按 EndPoint 复用连接） |
| `acceptor.h/cpp` | 连接接收器（accept + 注册到事件分发器） |
| `event_dispatcher.h/cpp` + `_epoll.cpp` + `_kqueue.cpp` | **事件分发器**：Linux 用 epoll，macOS/BSD 用 kqueue（1.17 起原生双实现） |
| `input_messenger.h/cpp` | 输入消息分派（把读到的字节按协议解析成消息） |
| `protocol.h/cpp` | **协议框架**：协议注册表、消息解析/封装抽象（`Protocol` 结构） |
| `transport.h` + `tcp_transport.h/cpp` + `rdma_transport.h/cpp` | 传输层：TCP / RDMA 两种实现 |
| `global.h/cpp` | 全局状态（模块初始化/销毁） |

### 4.2 客户端进阶能力
| 文件 | 功能 |
|---|---|
| `load_balancer.h/cpp` | 负载均衡框架（`LoadBalancer` 基类 + 注册表） |
| `naming_service.h` | 命名服务框架 |
| `retry_policy.h/cpp` | 重试策略 |
| `backup_request_policy.h/cpp` | **备份请求**（Backup Request：超时前发第二个请求） |
| `circuit_breaker.h/cpp` | **熔断器**（连续失败熔断） |
| `cluster_recover_policy.h/cpp` | 集群恢复策略 |
| `channel_base.h` | 通道基类 |
| `parallel_channel.h/cpp` | **并行通道**（一次调用拆成多个子调用） |
| `partition_channel.h/cpp` | **分区通道**（按 key 分区） |
| `selective_channel.h/cpp` | **选择通道**（按需切服务） |

### 4.3 服务端进阶能力
| 文件 | 功能 |
|---|---|
| `adaptive_max_concurrency.h/cpp` | 自适应最大并发（CPU 使用率自动调整） |
| `concurrency_limiter.h` | 并发限制框架（接口） |
| `periodic_task.h/cpp` | 周期任务 |
| `stream.h/cpp` + `stream_impl.h` | **流式 RPC**（streaming 通道） |
| `server_id.h/cpp` | Server 唯一 ID |

### 4.4 通用抽象
| 文件 | 功能 |
|---|---|
| `compress.h/cpp` | 压缩框架（注册 gzip/snappy 等） |
| `authenticator.h` | 认证框架（接口） |
| `restful.h/cpp` | **RESTful** 风格 URL 路由 |
| `uri.h/cpp` | URI 解析 |
| `http_header.h/cpp` / `http_method.h/cpp` / `http_status_code.h/cpp` | HTTP 基础类型 |
| `http2.h/cpp` | HTTP/2 帧/流实现 |
| `grpc.h/cpp` | gRPC 协议适配 |
| `rtmp.h/cpp` | RTMP 协议适配 |
| `trackme.h/cpp` | TrackMe 上报 |
| `span.h/cpp` | 链路追踪 span（配合 rpcz） |
| `rpc_dump.h/cpp` | RPC 抓包 dump（离线重放用） |
| `checksum.h/cpp` | 校验和 |
| `serialized_request.h/cpp` / `serialized_response.h/cpp` | 序列化请求/响应 |
| `extension.h` / `extension_inl.h` | 扩展注册机制 |
| `callback.h` / `closure_guard.h` | 回调 RAII 守卫 |
| `shared_object.h` / `destroyable.h` / `describable.h` | 基础对象 |
| `nshead.h` / `nshead_message.h/cpp` / `nshead_service.h/cpp` | nshead 协议 |
| `progressive_attachment.h/cpp` / `progressive_reader.h` | 渐进式读取 |
| `simple_data_pool.h/cpp` | 简单数据池 |
| `versioned_ref_with_id.h` | 版本化引用（ABA 防护） |
| `reloadable_flags.h/cpp` | 可热加载配置 |
| `amf.h/cpp` | AMF 编解码（flash 协议） |
| `redis.h/cpp` + `redis_command.h/cpp` + `redis_reply.h/cpp` + `redis_cluster.h/cpp` | **Redis 客户端** |
| `memcache.h/cpp` | **Memcached 客户端** |
| `couchbase.h/cpp` | Couchbase 客户端 |
| `mongo_service_adaptor.h` | MongoDB 服务适配 |
| `thrift_message.h/cpp` + `thrift_service.h/cpp` | **Thrift 协议适配** |
| `adaptive_connection_type.h` / `adaptive_protocol_type.h` | 自适应连接/协议类型 |
| `pb_compat.h` | protobuf 版本兼容 |
| `errno.proto` / `options.proto` / `builtin_service.proto` / `proto_base.proto` / `span.proto` / `rpc_dump.proto` / `rtmp.proto` / `trackme.proto` / `streaming_rpc_meta.proto` / `nshead_meta.proto` / `get_favicon.proto` / `get_js.proto` / `grpc_health_check.proto` | 各类 proto 定义 |

### 4.5 `brpc/details/` — 内部实现细节
| 文件 | 功能 |
|---|---|
| `controller_private_accessor.h` / `server_private_accessor.h` | 私有成员访问器（内部测试/插件用） |
| `http_message.h/cpp` / `http_parser.h/cpp` | HTTP/1 消息与解析器 |
| `hpack.h/cpp` + `hpack-static-table.h` | HTTP/2 HPACK 头压缩 |
| `health_check.h/cpp` | 健康检查（对端探活） |
| `naming_service_thread.h/cpp` | **命名服务后台线程**（异步刷新服务列表） |
| `load_balancer_with_naming.h/cpp` | 命名服务 + 负载均衡结合层 |
| `method_status.h/cpp` | 方法级状态统计 |
| `ssl_helper.h/cpp` / `mesalink_ssl_helper.cpp` | SSL/TLS 辅助 |
| `jemalloc_profiler.h/cpp` / `tcmalloc_extension.h/cpp` | 内存分配器扩展（profiling） |
| `usercode_backup_pool.h/cpp` | 用户代码备份池（保底线程池） |
| `sparse_minute_counter.h` | 稀疏分钟计数器 |
| `rtmp_utils.h/cpp` | RTMP 工具 |
| `has_epollrdhup.cpp` | 运行时探测内核是否支持 EPOLLRDHUP |

### 4.6 `brpc/policy/` — 协议/策略实现（最多子类的一层）
按功能分四组：

**① 协议实现（wire protocol）**
- `baidu_rpc_protocol` — **baidu_std 默认协议**（`baidu_rpc_meta.proto`）
- `hulu_pbrpc_protocol` / `public_pbrpc_protocol` / `sofa_pbrpc_protocol` / `nova_pbrpc_protocol` — 各类 pbrpc 兼容协议
- `nshead_protocol` / `nshead_mcpack_protocol` / `ubrpc2pb_protocol` — nshead/ubrpc 系列
- `http_rpc_protocol` — HTTP/1；`http2_rpc_protocol` — HTTP/2
- `redis_protocol` — Redis；`memcache_binary_protocol` — Memcached；`mongo_protocol` — MongoDB；`couchbase_protocol` — Couchbase
- `esp_protocol` — ESP；`thrift_protocol` — Thrift；`streaming_rpc_protocol` — 流式；`rtmp_protocol` — RTMP
- `most_common_message.h` — 通用消息结构

**② 命名服务（Naming Service）**
- `baidu_naming_service`（百度 BNS）/ `consul_naming_service`（Consul）/ `nacos_naming_service`（Nacos）/ `discovery_naming_service`（北极星 Discovery）/ `domain_naming_service`（DNS）/ `file_naming_service`（文件）/ `list_naming_service`（列表）/ `remote_file_naming_service`（远程文件）

**③ 负载均衡（Load Balancer）**
- `randomized_load_balancer`（随机）/ `weighted_randomized_load_balancer`（加权随机）/ `round_robin_load_balancer`（轮询）/ `weighted_round_robin_load_balancer`（加权轮询）/ `consistent_hashing_load_balancer`（一致性哈希）/ `locality_aware_load_balancer`（本地感知 LALB）/ `dynpart_load_balancer`（动态分区 DP）/ `hasher`（MD5/murmur 哈希函数）

**④ 其它策略**
- 并发限制：`auto_concurrency_limiter`（自适应）/ `constant_concurrency_limiter`（固定）/ `timeout_concurrency_limiter`（超时型）
- 压缩：`gzip_compress` / `snappy_compress`
- 校验：`crc32c_checksum`
- 认证：`giano_authenticator` / `esp_authenticator` / `couchbase_authenticator` / `redis_authenticator`
- `dh`（Diffie-Hellman 密钥交换）

### 4.7 `brpc/builtin/` — 内置 Web 服务（HTTP 调试面板）
| 服务 | 路径 | 功能 |
|---|---|---|
| `status_service` | `/status` | 服务状态概览 |
| `vars_service` | `/vars` | 查看所有 bvar 变量 |
| `connections_service` | `/connections` | 连接列表 |
| `sockets_service` | `/sockets` | socket 详细信息 |
| `flags_service` | `/flags` | gflags 查看/修改 |
| `threads_service` | `/threads` | 线程列表 |
| `bthreads_service` | `/bthreads` | bthread 列表 |
| `list_service` | `/list` | 服务列表 |
| `protobufs_service` | `/protobufs` | proto 信息 |
| `pprof_service` | `/pprof` | CPU/内存 profile |
| `rpcz_service` | `/rpcz` | RPC 调用追踪 |
| `hotspots_service` | `/hotspots` | 热点分析 |
| `memory_service` | `/memory` | 内存统计 |
| `ids_service` | `/ids` | 资源 ID |
| `dir_service` | `/dir` | 目录浏览 |
| `health_service` | `/health` | 健康检查 |
| `version_service` | `/version` | 版本信息 |
| `vlog_service` | `/vlog` | 日志级别调整 |
| `index_service` | `/` | 首页 |
| `prometheus_metrics_service` | `/metrics` | Prometheus 指标导出 |
| `grpc_health_check_service` | `/grpc.health.v1.Health/Check` | gRPC 健康检查 |
| `bad_method_service` | — | 兜底错误处理 |
| 内嵌资源 | `get_js_service` / `get_favicon_service` + `jquery_min_js` / `sorttable_js` / `flot_min_js` / `viz_min_js` / `pprof_perl` | 内嵌前端资源 |

### 4.8 `brpc/rdma/` — RDMA 传输
- `rdma_endpoint` — RDMA 端点；`rdma_helper` — 初始化辅助；`block_pool` — RDMA 内存块池

---

## 5. json2pb/ — JSON <-> Protobuf
| 文件 | 功能 |
|---|---|
| `json_to_pb.h/cpp` | JSON 解析为 protobuf 消息 |
| `pb_to_json.h/cpp` | protobuf 序列化为 JSON |
| `protobuf_map.h/cpp` | map 字段支持 |
| `protobuf_type_resolver.h/cpp` | 类型解析（按字段反查 schema） |
| `rapidjson.h` | 内嵌 rapidjson 库 |
| `encode_decode.h/cpp` / `zero_copy_stream_reader.h` / `zero_copy_stream_writer.h` | 编解码辅助 |

## 6. mcpack2pb/ — mcpack 二进制 <-> Protobuf
| 文件 | 功能 |
|---|---|
| `mcpack2pb.h/cpp` | 主入口 |
| `parser.h/cpp` + `parser-inl.h` | mcpack 解析 |
| `serializer.h/cpp` + `serializer-inl.h` | mcpack 序列化 |
| `field_type.h/cpp` | 字段类型定义 |
| `generator.h/cpp` | 按 proto 描述生成序列化代码 |

---

## 7. 外围

| 目录 | 功能 |
|---|---|
| `tools/` | `rpc_replay`（重放 dump）、`rpc_view`（调试面板）、`rpc_press`（压测）、`rpc_prof`（性能分析）等 CLI 工具 |
| `example/` | 官方示例（echo、streaming、http、redis、thrift、rdma 等 60+ cpp + 23 proto） |
| `test/` | 单元/集成测试（126 cpp + 88 cc） |
| `java/` | Java 客户端 |
| `python/` | Python 客户端 |
| `docs/` | 官方文档（146 md） |
| `build-sys/` / `cmake/` / `bazel/` / `package/` / `homebrew-formula/` | 构建系统相关 |
| `community/` | 社区材料 |

---

## 依赖关系图

```
butil  ──┬──> bthread  ──> brpc
         └──> bvar    ──> brpc
json2pb / mcpack2pb ──> brpc（序列化插件）
brpc 内部：protocol(框架) ──> policy(具体协议实现)
```

一句话概括：**butil 是底座，bthread 提供 M:N 协程调度，bvar 提供观测，brpc 把三者组合成完整的 RPC 框架，json2pb/mcpack2pb 是序列化插件，policy 目录是"协议/策略插槽"里可插拔的具体实现。**
