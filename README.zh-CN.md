# EdgeSentinel：Linux/C++20 工业设备健康监控与故障保护系统

EdgeSentinel 是一个个人学习与求职展示项目。它模拟部署在电机、泵或风机旁的 Linux 边缘控制器，通过温度、振动、电流和传感器在线状态判断设备健康程度，并在持续告警、严重故障或内部资源异常时进入降级、锁定故障或安全停机状态。

[English README](README.md) · [学习文档目录](docs/zh-CN/README.md)

> 真实性说明：当前没有连接真实电机或物理 UART/CAN/I²C 设备。模拟器、协议编解码、内存池、状态机、并发运行时和 Unix Socket 路径有自动化验证；Linux 硬件适配器目前属于“Ubuntu 编译通过、等待真实设备验证”。

## 为什么这个项目有实际意义

嵌入式程序并不是为了展示线程和内存池而创建线程和内存池。这里每个技术选择都有业务原因：

- 采集不能阻塞保护逻辑，因此传感器任务与控制任务分离。
- 设备状态只能有一个写入者，因此状态机归控制线程独占。
- 边缘设备内存有限，因此事件池和队列都有固定容量。
- 生产速度可能超过消费速度，因此队列提供背压而不是无限增长。
- 单次噪声不能触发停机，因此状态机使用连续样本计数和回差。
- 严重故障不能自动“抖回正常”，因此故障锁定后必须人工复位。
- 本地维护工具需要查询和注入故障，因此使用权限为 `0600` 的 Unix Domain Socket。

## 四线程职责

1. `Sensor task`：从模拟器或设备适配器读取采样并发布事件。
2. `Control task`：消费事件，是状态机唯一写入者。
3. `Monitor task`：周期采集内存池、队列和延迟指标。
4. `Unix socket task`：处理 `status`、`metrics`、`inject`、`reset` 和 `shutdown`。

核心事件路径为：

```text
FixedObjectPool<PooledEvent, 256>
    -> move-only RAII Handle
    -> BoundedQueue<Handle, 128>
    -> Control task
    -> Handle析构并归还内存块
```

初始化阶段仍会使用标准库和堆内存。准确描述是“关键事件通路在初始化后使用固定容量对象池”，而不是“整个程序完全不使用 `new/delete`”。

## 快速体验

Ubuntu：

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build
./scripts/build_ubuntu.sh
./scripts/run_demo.sh
```

演示会自动执行：

```text
启动服务 -> 查询Healthy -> 注入105摄氏度 -> 进入FaultLatched
-> 查询指标 -> 清除故障 -> 手动复位 -> 回到Healthy -> 安全停机
```

Windows 可以验证跨平台核心：

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
./out/build/windows-msvc-debug/Debug/edge_sentinel.exe --duration-ms 3000 --fault-after-ms 1000
```

## 常用命令

```bash
./out/build/linux-gcc-debug/edge_sentinel_daemon

./out/build/linux-gcc-debug/edgectl status
./out/build/linux-gcc-debug/edgectl metrics
./out/build/linux-gcc-debug/edgectl inject temperature 105
./out/build/linux-gcc-debug/edgectl inject vibration 12
./out/build/linux-gcc-debug/edgectl inject current 24
./out/build/linux-gcc-debug/edgectl inject offline
./out/build/linux-gcc-debug/edgectl clear
./out/build/linux-gcc-debug/edgectl reset
./out/build/linux-gcc-debug/edgectl shutdown
```

## 如何学习

建议不要先从 `main()` 顺序阅读全部代码，而是沿着测试逐层推进：

1. `tests/memory_pool_test.cpp`
2. `tests/bounded_queue_test.cpp`
3. `tests/state_machine_test.cpp`
4. `tests/runtime_test.cpp`
5. `tests/command_parser_test.cpp`
6. `tests/protocols_test.cpp`
7. `scripts/ci_ipc_smoke.sh`

配套文档位于 [docs/zh-CN](docs/zh-CN/README.md)。每篇都说明设计原因、代码入口、常见错误和练习题。

## 测试与性能

```bash
ctest --preset linux-gcc-debug
./out/build/linux-gcc-debug/edge_sentinel_benchmark
```

基准输出包含吞吐量、平均/P50/P95/P99/最大延迟、池与队列高水位、分配失败数。只有在同一构建类型、机器和负载下才适合横向比较。

GitHub Actions 当前检查：

- GCC 与 Clang 严格告警构建。
- 单元及多线程集成测试。
- Unix Socket 守护进程端到端测试。
- AddressSanitizer 与 UndefinedBehaviorSanitizer。
- ThreadSanitizer。

## 简历定位

推荐项目名：

> EdgeSentinel：Linux/C++20 多线程工业设备健康监控与故障保护系统（个人项目）

简历日期应使用真实开发时间。项目可以描述为工业边缘控制“仿真系统”，不要写成真实产线部署，也不要声称已经在物理电机、CAN 卡或 I²C 传感器上验证。
