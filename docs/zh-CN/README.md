# EdgeSentinel 中文学习路线

这组文档不是独立的 C++ 教程，而是解释仓库中的代码为什么这样设计。建议每读一篇文档，就运行对应测试并修改一个练习。

1. [现代 C++20 基础](01-cpp20-basics.md)
2. [固定对象池与对象生命周期](02-memory-pool.md)
3. [多线程架构与有界队列](03-concurrency.md)
4. [事件驱动状态机](04-state-machine.md)
5. [Linux IPC 与 RAII 文件描述符](05-linux-ipc.md)
6. [UART、CAN 与 I2C](06-linux-buses.md)
7. [调试、Sanitizer 与性能分析](07-debugging-profiling.md)
8. [源码阅读顺序](08-code-walkthrough.md)
9. [简历与面试指南](09-interview-guide.md)

## 推荐循环

```text
先读测试 -> 预测实现 -> 读实现 -> 单步调试 -> 修改一个边界 -> 重跑测试
```

不要背代码。面试真正关心的是：约束是什么、为什么这样设计、失败时会发生什么、你如何证明代码正确。
