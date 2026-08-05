# 04 事件驱动状态机

代码入口：

- `include/edge_sentinel/domain/event.hpp`
- `include/edge_sentinel/domain/state.hpp`
- `include/edge_sentinel/fsm/motor_state_machine.hpp`
- `tests/state_machine_test.cpp`

## 状态与事件分离

状态表示系统当前所处阶段，事件表示刚刚发生了什么。

状态包括：

- `Booting`
- `SelfTest`
- `Healthy`
- `Degraded`
- `FaultLatched`
- `SafeShutdown`

事件包括启动、自检成功、传感器采样、人工复位、内部资源故障和停机请求。

不要把“温度 105”做成一种状态。它是采样事件的数据，状态机根据配置和历史决定是否转换。

## 连续样本过滤

单个高值可能是电磁干扰或采样毛刺。默认配置要求：

- 连续 3 个 warning 样本才从 Healthy 进入 Degraded。
- 连续 2 个 critical 样本才进入 FaultLatched。
- 连续 3 次离线才认为传感器故障。

只要中间出现不满足条件的样本，相应计数清零。

## 回差避免状态抖动

温度 warning 阈值为 80°C，恢复阈值为 75°C。

设备在 80°C 进入降级后，不会因为下一次读到 79.9°C 就恢复。它必须低于 75°C 并持续满足恢复样本数。

```text
上升: 80及以上 -> warning
下降: 75及以下 -> recovered
75到80之间 -> 保持当前状态
```

这就是 hysteresis。它在恒温器、风扇控制和机械保护中非常常见。

## 锁定故障

进入 `FaultLatched` 后，正常样本不会自动恢复系统。必须收到 `reset`：

```text
FaultLatched -> SelfTest -> Healthy
```

这样可以防止严重故障条件短暂消失后设备自行重启。真实系统通常还会保存故障码和要求现场确认。

## 内部故障也是业务事件

内存池耗尽、关键队列不可用等资源问题最终也应转换成保护事件，而不是只打印日志继续运行。

当前运行时会记录池和队列失败；状态机已经提供 `internal_fault` 事件。后续真实产品可以增加“连续资源失败 N 次后发布内部故障”的策略。

## 转换返回值

`handle()` 返回 `StateTransition`，包括：

- previous
- current
- reason
- changed

调用者无需再次猜测是否发生转换，日志和指标也能记录明确原因。

## 练习

1. 给电流 warning 增加边界测试：刚好等于阈值应该如何？
2. 增加 `Maintenance` 状态，并先画转换图。
3. 让自检失败进入锁定故障，先写失败测试。
4. 思考多个传感器同时 warning 时是否需要记录具体来源。
