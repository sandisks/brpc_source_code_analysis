# Futex 是什么

`futex` 全称 **Fast Userspace Mutex（快速用户态互斥量）**，是 Linux 内核提供的一个同步原语，用来解决"线程锁在无竞争时不该付出系统调用代价"的问题。

## 核心思想

一把锁通常有两种实现方式的矛盾：

| 方式 | 优点 | 缺点 |
|---|---|---|
| 用户态自旋锁（spinlock） | 无系统调用，极快 | 有竞争时忙等烧 CPU，且无法让线程挂起 |
| 内核锁（pthread_mutex） | 能让线程挂起、让出 CPU | 每次 lock/unlock 都是系统调用，即使无竞争也贵 |

futex 把两者结合：**无竞争时走用户态原子操作，有竞争时才进内核**。

## 工作流程

```
lock:                      unlock:
  原子 CAS 尝试加锁           原子操作把锁值改回 0
  ├─ 成功 → 直接进临界区       如果有线程在等（内核等待队列非空）
  └─ 失败 → 调用 futex_wait      → 调用 futex_wake 唤醒它们
            (addr, expected)
```

- `futex_wait(addr, expected)`：系统调用，**先检查 `*addr` 是否仍等于 `expected`**（防止丢失唤醒），相等才挂起当前线程。这个"先检查再挂起"是原子的，解决了 trylock 和 wait 之间的竞态窗口。
- `futex_wake(addr, n)`：唤醒在 `addr` 上挂起的至多 n 个线程。

无竞争时 lock/unlock 全程不碰内核，所以比 pthread_mutex 快一个量级。

## 和 brpc 的关系

- brpc 的 bthread 同步原语 **butex**，在 Linux 上底层就是 futex（`src/bthread/butex.cpp`），这也是 bthread 挂起/唤醒能做到比 pthread 轻量的关键。
- macOS/BSD 没有 futex，所以 brpc 在非 Linux 平台上用 **pthread mutex + condition variable 模拟** futex 语义——就在 `src/bthread/sys_futex.cpp` 的 `SimuFutex` 类里（一个地址映射到一个 mutex+cond，配合等待计数 `counts`）。

## 项目里的参考

项目仓库有专门讲这个的文档：`docs/futex.md`，里面详细对比了 spinlock 与内核锁的不足、futex 设计原理，以及 brpc 中 `futex_wait_private` / `futex_wake_private` 的逐行解析。
