# 7. IOBuf（零拷贝缓冲区）

> 来源：https://brpc.apache.org/docs/c++-base/iobuf/ （bRPC 1.17.0）
> 定位：bRPC 内部的"高性能字节容器"。C++ 初学者向，配 Java 类比。

## 7.1 为什么不用 std::string / std::vector<char>

网络收发大量数据时，反复 `memcpy` 到 `std::string` 很浪费。bRPC 用 `IOBuf`：
- 内部由**多个不连续的内存块（Block）**组成，像链表。
- 拼接、切片、追加都是 O(1) 的指针操作，**几乎零拷贝**。
- 类比 Java 的 `Netty.ByteBuf` / `CompositeByteBuf`（零拷贝拼包）。

> Java 熟手类比：`IOBuf` ≈ Netty 的 `ByteBuf`（有读写指针、引用计数、可切片）；`IOPortal` ≈ `CompositeByteBuf`（把多个 buffer 拼成一个逻辑整体）。

## 7.2 基本用法

```cpp
#include <butil/iobuf.h>

butil::IOBuf buf;
// 追加数据（来自字符串/内存/文件描述符）
buf.append("hello");
buf.append(" world");

// 读取（拷贝出 N 字节）
char out[32];
size_t n = buf.cut_into(out, sizeof(out));   // 切走前 n 字节
// 长度
size_t len = buf.length();

// 追加另一个 IOBuf（零拷贝，只调整链表指针）
butil::IOBuf other;
other.append("tail");
buf.append(other);
```

## 7.3 常用操作速查

| 操作 | 说明 |
|------|------|
| `buf.append(data, len)` | 追加 |
| `buf.append(other_iobuf)` | 零拷贝拼接 |
| `buf.cut_into(buf2, n)` | 切走前 n 字节给 buf2 |
| `buf.cut_into(char*, n)` | 切走并拷贝到裸指针 |
| `buf.peek()` | 只读首字节，不消费 |
| `buf.pop_front(n)` | 丢弃前 n 字节 |
| `buf.length()` / `buf.empty()` | 长度 / 判空 |
| `buf.clear()` | 清空 |
| `buf >> x` | 按网络序解析基本类型（类似序列化） |

## 7.4 在 RPC 里用到 IOBuf 的地方

- **附件（Attachment）**：`cntl.request_attachment()` 返回 `IOBuf&`。当你要传大块二进制（如文件），绕过 protobuf 序列化直接走 IOBuf，性能更好。
- 服务端读原始请求体、写响应体，底层全是 IOBuf。

```cpp
// 客户端：附加大块数据，不走 proto 序列化
brpc::Controller cntl;
cntl.request_attachment().append(big_binary_data, size);
stub.Echo(&cntl, &req, &res, nullptr);

// 服务端：读取附件
butil::IOBuf& attach = cntl->request_attachment();
```

## 7.5 引用计数注意

IOBuf 是**值语义但内部共享块**（copy-on-write 风格）。`IOBuf a = b;` 后 a、b 共享底层块，但追加/修改时会复制受影响块，不会互相污染。与 `std::shared_ptr` 不同，它是**浅拷贝 + 写时复制**，非常高效。

## 7.6 例子对应

本仓库 `examples/iobuf_demo/` 演示 append / cut_into / 拼接 / attachment 用法。
