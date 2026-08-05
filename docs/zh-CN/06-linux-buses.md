# 06 UART、CAN 与 I²C

代码入口：

- `include/edge_sentinel/protocols/uart_protocol.hpp`
- `include/edge_sentinel/protocols/can_protocol.hpp`
- `include/edge_sentinel/protocols/i2c_sensor.hpp`
- `include/edge_sentinel/linux/sensor_adapters.hpp`
- `src/linux/sensor_adapters.cpp`
- `tests/protocols_test.cpp`

当前 daemon 默认使用模拟传感器。三个 Linux 适配器已经实现并在 Ubuntu 上编译，但尚未作为 daemon 命令行选项，也没有物理设备验证。这是刻意保留的真实边界。

## UART

UART 是异步字节流，没有天然消息边界。一次 `read()` 可能得到半帧、多帧或从任意位置开始的数据，所以不能假设“一次 read 等于一条消息”。

项目帧为固定 12 字节：

| 偏移 | 长度 | 字段 |
|---:|---:|---|
| 0 | 2 | 魔数 `AA 55` |
| 2 | 1 | 协议版本 |
| 3 | 1 | 测量类型 |
| 4 | 2 | 大端序列号 |
| 6 | 4 | 大端有符号 milli-value |
| 10 | 2 | CRC16-CCITT |

温度 105.5°C 编码为整数 105500。使用定点整数避免在线路协议中直接传输不同平台表示细节相关的浮点字节。

### 增量解码

`UartFrameDecoder::feed()` 每次接收一个字节：

- 搜索第一魔数。
- 验证第二魔数。
- 收集固定长度。
- 检查版本、类型和 CRC。
- 错误后重新同步。

测试故意分片输入，并修改一个 payload 位验证 CRC 拒绝。

### `termios`

Linux UART 适配器：

- 使用 `O_NOCTTY`，防止设备成为控制终端。
- 使用 `O_NONBLOCK` 和 `poll`，限制等待时间。
- `cfmakeraw()` 关闭文本行处理。
- 显式设置波特率、`CLOCAL`、`CREAD`、停止位和流控。
- 仅支持 9600、57600、115200 三种明确波特率。

接入真实设备前要确认电平：TTL UART、RS-232 和 RS-485 不是同一电气标准，错误连接可能损坏硬件。

## CAN 与 SocketCAN

CAN 是消息总线，帧边界由内核保留。项目使用经典 CAN 数据帧并约定：

| CAN ID | 含义 | 数据 |
|---:|---|---|
| `0x101` | 温度 | 4 字节大端 milli-value |
| `0x102` | 振动 | 4 字节大端 milli-value |
| `0x103` | 电流 | 4 字节大端 milli-value |
| `0x104` | 在线状态 | 1 字节布尔值 |

SocketCAN 把 CAN 接口暴露为 `PF_CAN` socket。适配器通过 `SIOCGIFINDEX` 查询接口索引，再 `bind()` 到指定接口。

### 无硬件创建 vcan

```bash
./scripts/setup_vcan.sh
ip -details link show vcan0
```

该脚本需要 `sudo`，因为创建网络接口和加载内核模块是特权操作。

安装 `can-utils` 后可观察或发送帧：

```bash
sudo apt install -y can-utils
candump vcan0
cansend vcan0 101#00019C1C
```

`00019C1C` 等于十进制 105500，即 105.5°C。

## I²C

I²C 是主从式同步总线。Linux 通常通过 `/dev/i2c-N` 和 `i2c-dev` 操作。

项目寄存器约定：

| 寄存器 | 长度 | 含义 |
|---:|---:|---|
| `0x01` | 2 | 有符号大端温度，单位 0.01°C |
| `0x03` | 2 | 无符号大端振动，单位 0.01 mm/s |
| `0x05` | 2 | 无符号大端电流，单位 0.01 A |
| `0x07` | 1 | bit0 表示在线 |

`LinuxI2cBus` 使用 `I2C_RDWR` 组合事务：先写寄存器地址，再执行 repeated-start 读取。相比两个彼此独立的 `write/read` 调用，组合事务能保持总线操作原子性。

### 为什么抽象 `II2cBus`

寄存器解码不应该依赖真实 `/dev/i2c-*`。测试中的 `FakeI2cBus` 返回固定字节，可以验证字节序、缩放和短路错误处理。

真实设备仍需要确认：

- 7 位地址。
- 寄存器宽度和字节序。
- 是否支持 repeated-start。
- 上拉电阻和电压。
- 传感器上电及转换时间。

## 验证等级

“协议测试通过”不等于“真实硬件可用”。物理验证还包含接线、电平、终端电阻、波特率、内核驱动和设备手册差异。

面试时应准确回答：协议层与 Linux 系统调用适配器已完成，模拟和虚拟路径可验证，物理设备尚未验证。

## 练习

1. 用 UART 编码器生成负温度帧，验证二进制补码解码。
2. 给 CAN 增加 DLC 不足测试。
3. 模拟 I²C 某一个寄存器读取失败，确认样本变为 offline。
4. 为 daemon 增加 `--sensor can:vcan0`，先设计错误提示和退出行为。
