# 07 调试、Sanitizer 与性能分析

## Debug 构建

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug
ctest --preset linux-gcc-debug
```

CTest 的 `--output-on-failure` 已写入 preset。只运行一个测试：

```bash
ctest --test-dir out/build/linux-gcc-debug -R memory_pool_test --output-on-failure
```

## GDB

```bash
gdb --args ./out/build/linux-gcc-debug/edge_sentinel --duration-ms 3000
```

建议断点：

```gdb
break edge_sentinel::fsm::MotorStateMachine::handle
break edge_sentinel::runtime::EdgeRuntime::publish
run
info threads
thread apply all bt
```

模板和内联函数的断点名称可能受编译器符号影响，也可以直接使用 `break 文件:行号`。

## AddressSanitizer 与 UBSan

```bash
cmake --preset linux-asan
cmake --build --preset linux-asan
ctest --preset linux-asan
```

ASan 检测越界、use-after-free 等内存错误。UBSan 检测部分未定义行为，例如错误移位或无效转换。

Sanitizer 会改变内存布局和时间，不应用它的性能结果代表 Release 性能。

## ThreadSanitizer

```bash
cmake --preset linux-tsan
cmake --build --preset linux-tsan
ctest --preset linux-tsan
```

TSan 关注数据竞争。它不能证明没有死锁，也不会替你验证业务状态转换。

ASan 与 TSan 使用不同的运行时，项目故意提供分离的 build preset，不能在同一目标中同时开启。

## Valgrind

```bash
sudo apt install -y valgrind
valgrind --leak-check=full --show-leak-kinds=all \
  ./out/build/linux-gcc-debug/edge_sentinel --duration-ms 1000 --fault-after-ms 300
```

Valgrind 比原生执行慢很多。先用小工作负载定位问题，再扩大场景。

## `strace`

观察 Unix Socket：

```bash
strace -f -e trace=socket,bind,listen,poll,accept,recvfrom,sendto,close \
  ./out/build/linux-gcc-debug/edge_sentinel_daemon
```

观察 I²C 时重点关注 `openat` 和 `ioctl(I2C_RDWR)`。

## `perf`

Release 构建可以手动配置：

```bash
cmake -S . -B out/build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/release
perf stat ./out/build/release/edge_sentinel_benchmark
perf record -g ./out/build/release/edge_sentinel_benchmark
perf report
```

## 如何读基准结果

基准同时报告吞吐量和延迟分位数。平均值可能掩盖少量很慢的事件，因此要看 P95、P99 和最大值。

还要同时观察：

- 队列是否达到容量上限。
- 对象池峰值是否接近容量。
- 是否发生分配失败。
- Debug 还是 Release。
- 是否运行 Sanitizer。
- CPU 是否被其他程序占用。

一次本机 MSVC Debug 结果只能作为该机器的初始基线，不能直接写成 Linux 实时性能承诺。

## Windows 环境提示

本次开发环境曾同时存在 `PATH` 和 `Path` 两个环境变量。MSBuild 将它们装入不区分大小写的字典时失败。项目代码没有问题，修复方式是清理启动进程的重复环境变量，而不是修改 CMake 逻辑掩盖问题。

## 练习

1. 分别记录 Debug、Release、ASan 三种基准结果。
2. 把控制线程处理时间增加 100 微秒，观察 P99 和队列水位。
3. 用 `perf report` 找出基准热点。
4. 故意制造一次越界，只在实验分支观察 ASan 报告，然后恢复。
