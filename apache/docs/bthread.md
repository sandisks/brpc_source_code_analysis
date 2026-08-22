# 6. bthread（协程 / 用户态线程）

> 来源：https://brpc.apache.org/docs/bthread/bthread/ （bRPC 1.17.0）
> 定位：理解 bRPC 的并发模型。C++ 初学者向，配 Java 类比。

## 6.1 什么是 bthread

bthread 是 bRPC 的**M:N 协程**：
- **M** 个 bthread 调度到 **N** 个 pthread（N ≈ CPU 核数）。
- 一个 bthread 阻塞时（如等 RPC 返回），它不会占住 pthread，框架自动切到别的 bthread。
- 对开发者来说，写起来像"一个请求一个线程"那么简单，但实际资源消耗极低（每个 bthread 栈默认仅 32KB，可开百万级）。

> Java 熟手最贴切的类比：**Java 21 的虚拟线程（Project Loom）**。bthread 比它早了快十年，设计思想一致——"把阻塞变成廉价的"。

## 6.2 为什么 bRPC 用 bthread 而不是 pthread

- pthread 创建/切换成本高（默认栈 8MB），开 10 万线程不现实。
- 传统异步（回调地狱）难写难维护。
- bthread 兼得两者：写法同步化、性能接近异步。

## 6.3 基本用法

```cpp
#include <bthread/bthread.h>

void* worker(void* arg) {
    // 这里运行在一个 bthread 里
    LOG(INFO) << "hello from bthread, arg=" << *(int*)arg;
    return nullptr;
}

int main() {
    bthread_t tid;
    int value = 42;
    // 启动一个后台 bthread（类似 executor.submit）
    bthread_start_background(&tid, nullptr, worker, &value);
    // 等待它结束（类似 future.get()）
    bthread_join(tid, nullptr);
    return 0;
}
```

**要点**：
- `bthread_start_background`：异步启动（不阻塞当前线程）。
- `bthread_start_urgent`：立刻在当前 pthread 上跑（少一次切换，适合已知要马上执行的）。
- `bthread_join`：等结束。

## 6.4 bthread 与同步原语

bthread 自带"会让出"的锁/条件变量，不要用 pthread 的 `std::mutex` 阻塞 bthread（会卡住整个 pthread！）。bRPC 提供：

```cpp
bthread_mutex_t mtx;
bthread_mutex_lock(&mtx);
// ...临界区...
bthread_mutex_unlock(&mtx);

bthread_cond_t cond;
bthread_cond_wait(&cond, &mtx);   // 会切换走，不占 pthread
bthread_cond_signal(&cond);
```

> 重要：在 bthread 里若用 `std::mutex`/`pthread_mutex`，一旦锁被别的 bthread 占用且那个 bthread 也在同一个 pthread 上，会**死锁或卡顿**。优先用 bthread 系列原语。

## 6.5 和 brpc 的关系

- 你写的服务端 `Echo()` 方法，**本身就在 bthread 里被调用**，所以方法里可以直接做同步阻塞 RPC（调下游），不用担心性能。
- 客户端同步调用（`stub.Echo(..., nullptr)`）也运行在 bthread 上：阻塞的只是当前 bthread，pthread 可以去跑别的 bthread。
- 这就是 bRPC 能用"看似同步"的代码拿到"异步级"吞吐的根本原因。

## 6.6 例子对应

本仓库 `examples/bthread_demo/` 演示 `bthread_start_background` + `bthread_join` + bthread 版 mutex。
