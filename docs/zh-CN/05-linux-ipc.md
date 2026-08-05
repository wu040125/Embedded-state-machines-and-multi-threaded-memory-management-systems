# 05 Linux IPC 与 RAII 文件描述符

代码入口：

- `include/edge_sentinel/linux/unique_fd.hpp`
- `include/edge_sentinel/linux/unix_command_server.hpp`
- `src/linux/unix_command_server.cpp`
- `apps/edge_sentinel_daemon.cpp`
- `apps/edgectl_main.cpp`
- `tests/command_parser_test.cpp`
- `scripts/ci_ipc_smoke.sh`

## 为什么选择 Unix Domain Socket

守护进程和控制工具运行在同一台 Linux 设备上，不需要暴露 TCP 端口。Unix Socket 提供：

- 本机进程间双向通信。
- 文件系统路径和权限控制。
- 与普通 stream socket 相近的编程模型。
- 可用 `poll`、`accept`、`recv`、`send` 等标准系统调用。

默认路径包含当前用户 UID：

```text
/tmp/edge-sentinel-<uid>.sock
```

## Socket 生命周期

服务端启动顺序：

1. 检查路径长度是否能放入 `sockaddr_un::sun_path`。
2. 如果旧路径存在，只允许删除当前用户拥有的 Socket。
3. 创建带 `SOCK_CLOEXEC` 的文件描述符。
4. `bind()` 到路径。
5. 将权限设置为 `0600`。
6. `listen()`。
7. 通信线程使用 `poll()` 等待连接。

停止时请求线程退出、关闭监听描述符并删除自己创建的 Socket 文件。

## `UniqueFd`

文件描述符是整数，但它代表内核资源。复制整数不会复制资源所有权，两个对象同时 `close()` 会产生错误。

`UniqueFd`：

- 禁止复制。
- 允许移动。
- 析构时只关闭自己持有的描述符。
- 使用 `-1` 表示空状态。

它与 `std::unique_ptr` 的所有权语义相似，只是释放动作从 `delete` 变成 `close`。

## 命令解析安全

协议限制一条命令最多 256 字节，并采用白名单语法：

```text
status
metrics
inject temperature <number>
inject vibration <number>
inject current <number>
inject offline
clear
reset
shutdown
```

解析器不会调用 shell。数值必须完整解析、为有限值并落在目标允许范围内。`nan`、`999°C`、额外参数和未知命令都会被拒绝。

## 为什么服务端与解析器分离

Socket 层只负责可靠接收有限长度字节和发送响应；解析器是纯 C++ 逻辑，可以在 Windows 上单元测试。

这种分层使大部分错误不需要启动 Linux 进程才能复现，也避免把业务规则塞进系统调用循环。

## 端到端验证

CI 不只运行解析器测试，还真实启动：

```text
edge_sentinel_daemon -> Unix Socket -> edgectl
```

随后查询 Healthy、注入高温、确认 FaultLatched、读取指标并请求停机。

## 仍可改进的地方

- 当前一次连接处理一条短命令，适合维护控制面，不适合高吞吐遥测。
- 客户端处理是串行的；大量并发客户端需要连接池或事件循环。
- 真实产品应考虑 systemd socket activation、用户组权限和审计日志。
- 响应目前是简单键值文本，可以后续扩展版本字段，但没有真实需求前不引入 JSON 依赖。

## 练习

1. 增加 `help` 命令并为参数数量写测试。
2. 使用 `strace -f` 观察 daemon 的 `socket/bind/poll/accept`。
3. 将 Socket 权限改成错误值，解释潜在风险。
4. 给客户端增加接收超时，避免异常服务端永久阻塞。
