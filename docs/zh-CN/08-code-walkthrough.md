# 08 源码阅读顺序

## 第一层：对象所有权

先读 `tests/memory_pool_test.cpp`，再读 `fixed_object_pool.hpp`。

回答：

- 槽位何时成为对象？
- `Handle` 为什么不能复制？
- 构造抛异常后空闲链是否损坏？
- `high_watermark` 表示什么？

## 第二层：线程间转移

读 `tests/bounded_queue_test.cpp` 和 `bounded_queue.hpp`。

重点跟踪一个 move-only 值：生产者局部变量 -> 队列槽位 -> 消费者局部变量。

回答：

- 满队列如何背压？
- close 后已有数据是否丢失？
- 为什么通知通常在解锁后执行？
- 伪唤醒如何处理？

## 第三层：业务规则

读 `tests/state_machine_test.cpp` 和 `motor_state_machine.hpp`。

先画转换表，再对照代码。不要一开始陷入每个 `if`。

回答：

- warning 与 critical 的连续次数为何不同？
- 78°C 为什么不能立即从 Degraded 恢复？
- FaultLatched 为什么忽略正常样本？

## 第四层：多线程组合

读 `tests/runtime_test.cpp` 和 `edge_runtime.hpp`。

沿着一个采样事件：

```text
SimulatedSensor::read
-> EdgeRuntime::publish
-> FixedObjectPool::try_create
-> BoundedQueue::wait_push
-> control_loop
-> MotorStateMachine::handle
-> Handle析构归还池块
```

再单独沿着 `stop()` 阅读所有线程如何退出。

## 第五层：控制面

读 `command_parser_test.cpp`、`command.hpp`、`unix_command_server.cpp` 和两个 app。

区分：

- 网络/IPC 字节接收。
- 命令语法验证。
- 业务命令执行。
- 文本响应格式化。

## 第六层：总线协议

先读 `protocols_test.cpp`，再按 UART -> CAN -> I²C 顺序阅读。

UART 重点是流式重同步和 CRC；CAN 重点是 ID/DLC/字节序；I²C 重点是寄存器事务和可替换总线接口。

最后再读 `sensor_adapters.cpp`，把纯协议逻辑与 Linux 系统调用对应起来。

## 第七层：构建与自动验证

读：

- `CMakeLists.txt`
- `CMakePresets.json`
- `cmake/Sanitizers.cmake`
- `.github/workflows/ci.yml`
- `scripts/ci_ipc_smoke.sh`

理解“本机测试通过”和“Linux 等价验证通过”的区别。

## 建议的学习分支

每个练习创建独立分支，例如：

```bash
git switch -c study/add-maintenance-state
```

先提交失败测试，再提交实现。完成后比较两个提交，而不是直接复制最终答案。
