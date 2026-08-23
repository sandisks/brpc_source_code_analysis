# brpc 错误码封装机制与完整列表

> 源码位置：`src/brpc/errno.proto`（框架错误码定义）、`src/butil/errno.h` / `src/butil/errno.cpp`（描述注册与查询）、`src/brpc/controller.h`（Controller 承载）

## 1. 为什么需要一套独立错误码

RPC 是跨网络、跨语言（C++/Java/Go）调用，服务端错误要能"原样传回"客户端。POSIX errno 有两个问题：

1. **平台相关**：各系统 errno 值不一致（如 `EAGAIN` 在不同平台值不同）
2. **语义太窄**：POSIX 只有 100 多个错误，远不够描述 RPC 场景（"服务端过载"、"备份请求"、"SSL 错误"等）

所以 brpc 用 `errno.proto`（protobuf 枚举）定义了一套**稳定的框架错误码**：数值即协议的一部分，跨语言、跨版本不变。

## 2. 三层错误码来源

| 层次 | 定义位置 | 范围 | 说明 |
|---|---|---|---|
| 系统 errno | `/usr/include/errno.h` | 1~133（Linux） | 底层 syscall/IO 错误 |
| 框架错误码 | `src/brpc/errno.proto` | 客户端 1001-1018，服务端 2001-2007，RDMA 3001-3002 | RPC 协议错误 |
| 用户自定义 | `BAIDU_REGISTER_ERRNO` 宏 | [-32768, 32768) 内任意 | 业务错误描述 |

三者共用同一张 `errno_desc[]` 映射表（`butil/errno.cpp` 的静态数组），查询统一走 `berror()`。

## 3. 封装机制

### 3.1 框架错误码：errno.proto

```proto
syntax="proto2";
package brpc;

enum Errno {
    ENOSERVICE = 1001;  // Service not found
    ENOMETHOD  = 1002;  // Method not found
    // ... 见下方完整列表
}
```

- 用 proto 定义 → 自动生成 C++/Java/Go 枚举，**保证跨语言数值一致**
- 客户端侧错误（1001-1018）与服务端侧错误（2001-2007）分区，看数字就知道是哪一端出的问题

### 3.2 Controller 承载错误

RPC 全程错误都记录在 `Controller` 上：

```cpp
// 服务端：主动标记失败（SetFailed 后 Failed() 返回 true，错误会随响应传回客户端）
cntl->SetFailed(EINTERNAL, "db connect failed: %s", e.what());
// 等价写法：SetFailed(const std::string&)

// 客户端：调用返回后检查
if (cntl->Failed()) {
    LOG(ERROR) << "code=" << cntl->ErrorCode()          // int
               << ", msg=" << cntl->ErrorText();        // std::string
}
```

| API | 作用 |
|---|---|
| `SetFailed(int error_code, const char* fmt, ...)` | printf 风格设置失败（服务端用） |
| `SetFailed(const std::string& reason)` | 设置失败（内部错误码通常为 EINTERNAL） |
| `Failed()` | 是否失败 |
| `ErrorCode()` | 错误码（重试时后者覆盖前者） |
| `ErrorText()` | 完整错误描述（≠ `berror(ErrorCode())`，**包含 SetFailed 传入的 reason**） |

### 3.3 错误码 → 描述：butil::berror

`src/butil/errno.h` / `errno.cpp`：

```cpp
const char* berror(int error_code);  // 错误码转描述（TLS 缓冲，线程安全）
const char* berror();                // 取当前 errno 的描述
```

- 描述存放在静态表 `errno_desc[32768 - (-32768)]`，覆盖 [-32768, 32768)
- 未注册的码：回退到 `strerror_r()`（系统 errno）；系统也认不出 → `"Unknown error %d"`
- 查询结果写入 **TLS 线程局部缓冲**（`__thread char tls_error_buf[64]`），线程安全

### 3.4 注册自定义错误码：BAIDU_REGISTER_ERRNO

```cpp
#include <butil/errno.h>
#define EMYERROR 12345
BAIDU_REGISTER_ERRNO(EMYERROR, "my custom error");

// 使用
errno = EMYERROR;
printf("%s\n", berror());       // "my custom error"
```

重复定义保护（编译期或运行期前失败）：

```
"redefinition of `class BaiduErrnoHelper<12345>'"
// 或运行时
"Fail to define EMYERROR(12345) which is already defined as `...', abort"
```

注意：`%m` 等 printf 扩展**不识别** `BAIDU_REGISTER_ERRNO` 注册的错误，必须显式 `%s` + `berror()`。

## 4. 完整错误码列表（brpc 1.17.0）

### 4.1 客户端侧错误（1001-1018）

| 错误码 | 名称 | 含义 |
|---|---|---|
| 1001 | `ENOSERVICE` | Service not found，服务不存在 |
| 1002 | `ENOMETHOD` | Method not found，方法不存在 |
| 1003 | `EREQUEST` | Bad Request，请求格式错误 |
| 1004 | `ERPCAUTH` | Unauthorized，未授权（不能直接用 `EAUTH`，MACOSX 下已定义） |
| 1005 | `ETOOMANYFAILS` | Too many sub calls failed，并行子调用失败过多（ParallelChannel） |
| 1006 | `EPCHANFINISH` | [Internal] ParallelChannel finished |
| 1007 | `EBACKUPREQUEST` | Sending backup request，正在发备份请求 |
| 1008 | `ERPCTIMEDOUT` | RPC call is timed out，调用超时 |
| 1009 | `EFAILEDSOCKET` | Broken socket，连接损坏 |
| 1010 | `EHTTP` | Bad http call，HTTP 调用异常 |
| 1011 | `EOVERCROWDED` | The server is overcrowded，服务端过载 |
| 1012 | `ERTMPPUBLISHABLE` | RtmpRetryingClientStream is publishable |
| 1013 | `ERTMPCREATESTREAM` | createStream was rejected by the RTMP server |
| 1014 | `EEOF` | Got EOF，读到流结束 |
| 1015 | `EUNUSED` | The socket was not needed |
| 1016 | `ESSL` | SSL related error，SSL 相关错误 |
| 1017 | `EH2RUNOUTSTREAMS` | The H2 socket was run out of streams，HTTP/2 流耗尽 |
| 1018 | `EREJECT` | The Request is rejected，请求被拒绝（限流/熔断） |

### 4.2 服务端侧错误（2001-2007）

| 错误码 | 名称 | 含义 |
|---|---|---|
| 2001 | `EINTERNAL` | Internal Server Error，服务内部错误 |
| 2002 | `ERESPONSE` | Bad Response，响应格式错误 |
| 2003 | `ELOGOFF` | Server is stopping，服务正在下线 |
| 2004 | `ELIMIT` | Reached server's limit on resources，达到资源上限 |
| 2005 | `ECLOSE` | Close socket initiatively，主动关闭连接 |
| 2006 | `EITP` | Failed Itp response |
| 2007 | `ESHUTDOWNWRITE` | Shutdown write of socket |

### 4.3 RDMA 相关错误（3001-3002，客户端/服务端都可能发生）

| 错误码 | 名称 | 含义 |
|---|---|---|
| 3001 | `ERDMA` | RDMA verbs error，RDMA 操作错误 |
| 3002 | `ERDMAMEM` | Memory not registered for RDMA，内存未注册 |

## 5. 典型使用场景（对照仓库 examples）

```cpp
// 服务端（参照 examples/echo_server.cpp 风格）——主动标记失败
class EchoServiceImpl : public EchoService {
public:
    void Echo(google::protobuf::RpcController* cntl_base,
              const EchoRequest* request, EchoResponse* response,
              google::protobuf::Closure* done) override {
        brpc::ClosureGuard done_guard(done);          // 保证 done 必然执行
        brpc::Controller* cntl =
            static_cast<brpc::Controller*>(cntl_base);
        if (request->message().empty()) {
            cntl->SetFailed(brpc::EREQUEST, "empty message");
            return;
        }
        response->set_message(request->message());
    }
};
```

```cpp
// 客户端（参照 examples/echo_client.cpp 风格）——调用后统一检查
EchoService_Stub stub(&channel);
EchoRequest request;
EchoResponse response;
brpc::Controller cntl;
request.set_message("hello");
stub.Echo(&cntl, &request, &response, nullptr);

if (cntl.Failed()) {
    // 日志里同时看到数值码 + 可读描述
    LOG(ERROR) << "Fail to call Echo: " << cntl.ErrorCode()
               << " " << cntl.ErrorText();
    return -1;
}
```

## 6. 注意事项

1. **ErrorText() ≠ berror(ErrorCode())**：ErrorText 拼接了 `SetFailed` 时的 reason，包含完整上下文
2. **错误码重试覆盖**：Channel 自动重试后，`ErrorCode()` 是最后一次尝试的错误码
3. **HTTP 协议联动**：SetFailed 会根据 error_code 自动调整 `http_response().status_code()`；想覆盖需在 SetFailed **之后**调 `set_status_code()`
4. **`%m` 不识别自定义错误**：自定义错误码要打印描述必须 `printf("%s", berror())`
5. **数值是协议**：`errno.proto` 的枚举值改动属于协议变更，框架错误码只能追加保留位（1001-1018、2001-2007 之外预留了大量 `EUNUSED` 区间），不能用业务错误码占位
