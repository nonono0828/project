# STM32 与 ESP32-S3 UART 对接协议

文档版本：`1.3`

日期：`2026-07-25`

适用工程：`esp32s3_l610_ps2_v5`
传输方向：STM32 传感器/GPS 上报，ESP32-S3 PS2 按键下发

本文是 STM32 与 ESP32-S3 之间 UART 对接的主规范。新代码以本文为准；旧文档中的气体浓度对象、分离的 sensor/GPS 包以及旧裸 JSON 运动命令均不再作为新实现依据。

## 1. 当前实现状态

| 功能 | 方向 | 帧类型 | 当前 v5 状态 |
| --- | --- | ---: | --- |
| 传感器与 GPS 完整快照 | STM32 -> ESP32 | `0x01` | 已实现并已通过 TTL 注入测试 |
| ESP 独立云端心跳 | ESP32 -> 云服务器 | 不经过 STM32 UART | 已实现，2 秒一次 |
| AP 热点控制 | 云服务器 -> ESP32 | 不经过 STM32 UART | 已实现，由 ESP 自己处理 |
| 运动控制 | ESP32 -> STM32 | `0x02` | 已实现并通过 ESP-IDF 5.5.1 完整构建 |
| 控制执行 ACK | STM32 -> ESP32 | `0x03` | 不使用，保留类型号 |

重要说明：v5 固件保留已验证的 `type=0x01` 接收链路，只增加 `type=0x02` 下发，不要求 STM32 返回执行回执。固件已经完成构建，但仍需连接 STM32 做实机联调。

## 2. 硬件连接

正式固件使用 ESP32-S3 `UART1`：

| H750/SC16IS752 | ESP32-S3 | 方向 | 说明 |
| --- | --- | --- | --- |
| SC16 CH-B TXB | GPIO2 / UART1 RX | H750 -> ESP | 遥测帧 |
| SC16 CH-B RXB | GPIO1 / UART1 TX | ESP -> H750 | 控制命令 |
| GND | GND | 双向 | 必须共地 |

电气与串口参数：

- 逻辑电平：`3.3 V TTL`，不得把 5 V TTL 直接连接 ESP GPIO。
- 波特率：`115200`。
- 数据位：`8`。
- 校验位：无。
- 停止位：`1`。
- 流控：无。
- UART 为全双工，STM32 上报遥测时 ESP 可以同时下发控制。
- 不得在同一个 UART 输出 `printf` 调试日志，否则日志字节会破坏二进制帧。

ESP 的 USB/UART0 调试口只用于 PC 日志和注入测试，不是正式 H750
接口。H750 的唯一调试口是 UART8 PE1/TX、PE0/RX；调试文本不得进入
SC16 CH-B。

## 3. 统一二进制帧

所有消息使用同一个外层帧：

```text
A5 5A | version:u8 | type:u8 | json_len:u16be | seq:u32be |
UTF-8 JSON | crc16:u16be | 0D 0A
```

### 3.1 字段定义

| 偏移 | 长度 | 字段 | 规则 |
| ---: | ---: | --- | --- |
| 0 | 2 | magic | 固定 `A5 5A` |
| 2 | 1 | version | 固定 `0x01` |
| 3 | 1 | type | 见消息类型表 |
| 4 | 2 | json_len | JSON 的 UTF-8 字节数，大端；遥测最多 1024，H750 控制接收最多 96 |
| 6 | 4 | seq | 无符号 32 位序号，大端 |
| 10 | N | JSON | UTF-8、无 BOM，不包含 C 字符串结尾 `\0` |
| 10+N | 2 | CRC16 | 大端 |
| 12+N | 2 | tail | 固定 `0D 0A` |

总帧长度恒为：

```text
json_len + 14
```

### 3.2 消息类型

| type | 方向 | 用途 | 序号含义 |
| ---: | --- | --- | --- |
| `0x01` | STM32 -> ESP | 传感器与 GPS 完整快照 | `telemetry_seq` |
| `0x02` | ESP -> STM32 | PS2 按键状态 | `command_seq` |
| `0x03` | 保留 | 当前不使用 | - |
| `0x04..0xFF` | 保留 | 不得使用 | - |

`0x01` 与 `0x02` 使用各自独立的序号空间；`0x03` 当前不使用。

### 3.3 CRC

算法：`CRC-16/CCITT-FALSE`。

```text
poly    = 0x1021
init    = 0xFFFF
RefIn   = false
RefOut  = false
XorOut  = 0x0000
```

CRC 覆盖范围从 `version` 开始，到 JSON 最后一个字节结束，即：

```text
frame[2] ... frame[9 + json_len]
```

不包含 `A5 5A`、CRC 自身和 `0D 0A`。

标准自检：

```text
CRC16_CCITT_FALSE("123456789") = 0x29B1
```

### 3.4 接收超时与重同步

- 收到完整 `A5 5A` 后开始计时。
- `300 ms` 内未收齐完整帧，丢弃本帧并重新搜索帧头。
- 长度为 0、长度大于 1024、version/type 不支持、CRC 错误或帧尾错误都必须丢弃。
- 错误帧不得刷新运动看门狗，不得更新传感器数据。
- 接收器必须支持“一帧被拆成多次 DMA/中断到达”以及“一次收到多帧”。
- 不允许把 C 结构体直接映射到帧头；STM32 为小端，所有多字节整数必须显式转为大端。

## 4. STM32 上报传感器与 GPS（type `0x01`）

### 4.1 完整快照原则

每个 `0x01` 帧都是一次完整的当前快照，不是增量补丁。STM32 推荐每帧始终携带 `sensor` 和 `gps` 两个对象。某个字段无数据时发送 JSON `null`。ESP 和服务器兼容缺少对象或字段的帧，但缺少部分会在本帧中直接变成 `NA`，不会沿用上一帧旧值。

不要交替发送 sensor-only 和 gps-only 包。报警事件需要同一帧中的 GPS 才能记录当前位置。

推荐格式：

```json
{
  "sensor": {
    "temperature": 26.4,
    "humidity": 58.2,
    "altitude": 42.1,
    "pressure": 1008.6,
    "gas": 0,
    "person_detected": 0
  },
  "gps": {
    "lat": 39.904200,
    "lng": 116.407400,
    "satellites": 12,
    "speed": 0.0,
    "heading": 0.0
  }
}
```

实际发送时可以去掉空格和换行以缩短帧长。

### 4.2 字段类型、单位和范围

| 字段 | JSON 类型 | 单位/含义 | 有效范围 | 无数据 |
| --- | --- | --- | --- | --- |
| `sensor.temperature` | number | 摄氏度 `°C` | `-40.0..85.0` | `null` |
| `sensor.humidity` | number | 相对湿度 `%RH` | `0.0..100.0` | `null` |
| `sensor.altitude` | number | 米 `m` | `-500.0..10000.0` | `null` |
| `sensor.pressure` | number | 百帕 `hPa` | `300.0..1100.0` | `null` |
| `sensor.gas` | integer | `0=正常`，`1=报警` | 仅 `0/1` | `null` |
| `sensor.person_detected` | integer | `0=未发现`，`1=疑似幸存者` | 仅 `0/1` | `null` |
| `gps.lat` | number | WGS-84 纬度，十进制度 | `-90..90` | `null` |
| `gps.lng` | number | WGS-84 经度，十进制度 | `-180..180` | `null` |
| `gps.satellites` | integer | 使用卫星数 | `0..99` | `null` |
| `gps.speed` | number | 米每秒 `m/s` | `0..100` | `null` |
| `gps.heading` | number | 真北顺时针航向角，度 | `0 <= x < 360` | `null` |

约束：

- `gas` 只发送数字 `0/1`。旧版 `concentration/%LEL` 对象仅为兼容格式，新代码不要使用。
- `person_detected` 必须发送数字 `0/1`，不要发送 JSON `true/false`。
- 不得发送字符串数字、`"NA"`、空字符串、`NaN`、`Infinity` 或 `-999` 哨兵值。
- `lat` 和 `lng` 必须同时有效或同时为 `null`。
- 无 GPS 定位时建议 `lat/lng/speed/heading=null`；已知没有卫星可发 `satellites=0`，模块状态未知则发 `null`。
- 建议温湿度、海拔、气压保留 1 位小数，经纬度保留 6 位小数，速度和航向保留 1 至 2 位。

### 4.3 报警状态规则

- 气体或人员状态从 `0` 变为 `1` 时，应在 `100 ms` 内立即发送一帧，并携带当时的 GPS。
- 状态恢复后必须明确发送一次 `0`。
- `null` 只表示未知，不会解除前端报警锁存。
- 气体和人员同时报警时仍放在同一快照内；服务器分别建立两条事件，人员事件优先显示。

### 4.4 上报频率

- 周期上报推荐 `1 Hz`，即每 1000 ms 一帧。
- 状态变化立即额外上报，不必等待下一个周期。
- 通常不需要超过 `5 Hz`。
- 即使数值没有变化，周期帧也必须使用新序号。

### 4.5 telemetry_seq

- `seq` 可以从任意 `uint32` 值开始，0 合法，不要求从 0 或 1 开始。
- 每发送一帧新快照加一。
- `0xFFFFFFFF` 后自然回绕到 0。
- 重发完全相同的帧时沿用原序号；新采样必须使用新序号。

当前服务器按 `(ESP device_id, ESP boot_id)` 对遥测序号去重。若 STM32 单独复位而 ESP 没有复位，STM32 把序号重新变成 0/1 后，服务器会持续判为旧包。

当前兼容方案二选一：

1. STM32 使用 RTC 备份寄存器或备份 SRAM 保存下一序号；不要每秒写内部 Flash。
2. STM32 单独复位时同步复位 ESP，使 ESP 生成新的 `boot_id`。

后续协议升级应加入 `stm32_boot_id`，并把服务器去重键改为 `(device_id, esp_boot_id, stm32_boot_id)`。

## 5. ESP 下发 PS2 按键（type `0x02`）

### 5.1 端到端链路

```text
网页 -> WebSocket -> 云服务器 -> ESCTL/1 UDP -> ESP32
ESP32 -> type 0x02 UART -> STM32 -> 电机控制
```

云端包中的 `device_id` 和 `boot_id` 由 ESP 校验。ESP 只把已验证、目标正确的 PS2 按键状态转换成 UART `0x02` 帧。`ap_stream` 由 ESP 自己处理，不得转发给 STM32。

摄像头固定在机身上，视角随机器人机身运动。协议中不定义 `camera_left`、`camera_right`、`camera_pan` 或任何独立摄像头控制。

### 5.2 云端到 ESP

云服务器仍使用 ESCTL/1 绑定目标设备：

```json
{
  "protocol": "ESCTL/1",
  "kind": "command",
  "cmd": "ps2_button",
  "request_id": "web-1",
  "device_id": "esp32s3-01",
  "boot_id": "72A0AE84",
  "control_seq": 305419896,
  "params": {"button": "PAD_UP", "state": "down"}
}
```

服务器为每个 `device_id` 生成单调递增的 uint32 `control_seq`。进程首次
为某设备生成序号时，以当前 Unix 时间毫秒的低 32 位作为种子，避免服务器
重启后从 0 开始而被仍在线的 ESP 判为旧包。

ESP 校验 `device_id`、`boot_id`、`request_id`、`control_seq`、按键名和
状态。身份字段只用于云端到 ESP，不再传给 H750；`control_seq` 必须原样
透传到 H750。

### 5.3 ESP 到 STM32

UART JSON 固定包含三个字段：

```json
{
  "button": "PAD_UP",
  "state": "down",
  "control_seq": 305419896
}
```

松开同一个按键：

```json
{
  "button": "PAD_UP",
  "state": "up",
  "control_seq": 305419897
}
```

控制帧外层 `seq` 由 ESP 每帧加一，仅用于定位 ESP 到 H750 的串口问题。
内层 `control_seq` 是网页到 H750 的端到端运动顺序，ESP 和 H750 都必须
拒绝重复或旧值。H750 不维护 `session` 或 `request_id`。

### 5.4 四方向与摇杆轴映射

云端只提供四个方向，不提供模式、姿态、步态、急停或摄像头按键。H750
只在原有 `remote_deal()` 中将方向状态转换为两个现有摇杆轴，再调用原有
`velocity_cal()`；不得直接写步态或舵机。

| 前端按键 | UART `button` | H750 模拟值 | 机器人动作 |
| --- | --- | ---: | --- |
| 前 | `PAD_UP` | `RY=+127` | 向前移动 |
| 后 | `PAD_DOWN` | `RY=-128` | 向后移动 |
| 左 | `PAD_LEFT` | `LX=-128` | 原地左转或与前后组合转向 |
| 右 | `PAD_RIGHT` | `LX=+127` | 原地右转或与前后组合转向 |

前后轴和左右轴独立，可以同时按“前+左”“前+右”“后+左”或“后+右”。
同一轴上的相反方向互斥；H750 若收到同轴冲突状态，两个轴输出都按安全
规则归零。

除表中四个字符串外，服务器、ESP 和 H750 都必须拒绝其他
`ps2_button` 名称。

### 5.5 按住续命

- 第一次收到方向 `down` 后立即开始相应运动。
- 网页按住时每 200 ms 发送一次新的 `down`，每次由服务器分配新的
  `control_seq`。
- H750 只在通过 CRC、JSON 和新序号校验后刷新该方向的最后接收时间。
- 收到 `up` 立即释放对应方向。
- 连续 500 ms 没收到该方向新的有效 `down`，H750 自动释放。
- 云端四方向不做短按、长按语义，也不触发实体 PS2 的模式按键逻辑。

### 5.6 多按键与页面失联

- H750 为四个方向维护独立的 down/up 状态。
- 前端切换同轴方向时，先发送旧方向 `up`，再发送新方向 `down`。
- 前端切换另一轴方向时保留已按住轴，因此前后移动可以同时叠加左右转向。
- 若 STM32 同时看到互相冲突的方向键，按安全停止处理。
- 网页松键、窗口失焦或 WebSocket 断开时，应立即给所有当前按下按键发送 `up`。
- 即使 `up` 丢失，STM32 的 500 ms 超时也必须自动释放并停车。
- 实体 PS2 任一摇杆轴非零时立即清除云控。被抢占期间仍处于按下状态的
  云端方向必须先收到 `up`，之后新的 `down` 才能恢复运动。

## 6. 不使用控制回执

- STM32 不发送 `type=0x03`。
- ESP 不等待 STM32 ACK，也不重试等待回执。
- 云端 `sent` 只表示服务器已把 UDP 包发给 ESP，不能显示为“STM32 已执行”。
- STM32 是否安全停车由本地 500 ms 按键超时保证。

## 7. 控制序号

- 服务器给每个设备的每个有效四方向命令分配一个 uint32
  `control_seq`；同一毫秒内继续递增。
- ESP 先验证 ESCTL/1 目标身份，再用
  `(int32_t)(candidate - previous) > 0` 拒绝重复或旧 UDP 包。
- ESP 把同一个 `control_seq` 写入 UART JSON；不得重新编号。
- H750 再执行同样的回绕安全比较。旧包不得改变方向状态，也不得刷新
  500 ms 续命。
- ESP 自己递增的外层帧头 `seq` 与 `control_seq` 相互独立。
- ESP 或 H750 重启后都从“全部方向已释放”开始，不恢复旧运动状态。
- uint32 恰好相差 `0x80000000` 时顺序有歧义，按旧包拒绝。

## 8. 必须实现的本地安全策略

### 8.1 500 ms 按键超时

- 每个处于 down 状态的按键记录最后一次有效 `down` 时间。
- 新序号的重复 `down` 每 200 ms 刷新最后接收时间，但不能重置最初按下时间。
- 500 ms 没有新的 `down`，STM32 自动把该按键变为 up。
- 十字方向键超时后立即停车。
- 停车不能依赖 ESP、4G、服务器或浏览器仍在线。

这样任何一段断开，机器人最迟在 500 ms 内停止。

### 8.2 启动和复位

- STM32 上电后电机默认关闭。
- 在收到第一条有效方向键 down 前保持停止。
- ESP 重启后最多 500 ms 没有按键续命，STM32 自动释放并停车。
- STM32 复位后也必须保持停车，不能恢复复位前 PWM。

### 8.3 实体 PS2 优先

- 云端只能提供 `RY/LX` 两个运动轴，不能切换模式、姿态、步态或急停。
- 实体 PS2 任一摇杆轴非零时，H750 在当前控制周期立即使用实体摇杆。
- 实体抢占会清空云端运动；浏览器仍按住的方向必须先释放再重按。
- 本地硬件急停、驱动器故障和欠压保护不属于本云控协议，仍由 H750
  原有安全逻辑负责。

### 8.4 STM32 UART 发送

STM32 TX 只发送现有 `type=0x01` 遥测帧，不增加控制 ACK。现有遥测发送逻辑保持不变。

## 9. STM32 参考代码

### 9.1 CRC 与组帧

```c
#include <stddef.h>
#include <stdint.h>
#include <string.h>

uint16_t es_crc16_ccitt_false(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFu;

    while (length-- > 0u) {
        crc ^= (uint16_t)(*data++) << 8;
        for (uint8_t bit = 0; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void es_write_u16_be(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value >> 8);
    dst[1] = (uint8_t)value;
}

static void es_write_u32_be(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

/* 成功返回总帧长度，失败返回 0。 */
size_t es_build_frame(uint8_t type,
                      uint32_t sequence,
                      const uint8_t *json,
                      uint16_t json_length,
                      uint8_t *output,
                      size_t output_capacity)
{
    const size_t total_length = (size_t)json_length + 14u;

    if (json == NULL || output == NULL ||
        json_length == 0u || json_length > 1024u ||
        output_capacity < total_length) {
        return 0u;
    }

    output[0] = 0xA5u;
    output[1] = 0x5Au;
    output[2] = 0x01u;
    output[3] = type;
    es_write_u16_be(&output[4], json_length);
    es_write_u32_be(&output[6], sequence);
    memcpy(&output[10], json, json_length);

    const uint16_t crc =
        es_crc16_ccitt_false(&output[2], 8u + json_length);
    es_write_u16_be(&output[10u + json_length], crc);
    output[12u + json_length] = 0x0Du;
    output[13u + json_length] = 0x0Au;
    return total_length;
}
```

### 9.2 RX 状态机伪代码

```text
SEARCH_A5:
    byte == A5 -> SEARCH_5A

SEARCH_5A:
    byte == 5A -> READ_HEADER，并启动 300ms 定时
    byte == A5 -> 保持 SEARCH_5A
    其他       -> SEARCH_A5

READ_HEADER:
    收满 version/type/len/seq
    len 不在 1..1024 -> 丢弃并 SEARCH_A5

READ_BODY:
    收满 JSON + CRC + 0D0A
    超时、帧尾错、CRC错 -> 丢弃并 SEARCH_A5
    校验成功 -> 按 type 分发，再 SEARCH_A5
```

推荐使用 STM32 HAL `HAL_UARTEx_ReceiveToIdle_DMA()` 加环形缓冲，把每次收到的字节块逐字喂给状态机。对于带 D-Cache 的 STM32H7，DMA 缓冲必须放在非缓存区，或在读取前正确执行 cache invalidate。

### 9.3 JSON 生成

- 推荐使用固定大小字符数组和 `snprintf`，检查返回值是否小于缓冲区容量。
- JSON 长度使用 `strlen(json)`，不包含结尾 `\0`。
- 不使用动态内存构造周期遥测。
- 接收控制时先把全部字段解析到临时结构体并完成范围检查，再一次性修改电机状态；禁止边解析边驱动。

## 10. 在线与 NA 逻辑

STM32 不负责向云端发送心跳。ESP 在 L610 联网后每 2 秒独立发送心跳：

- 最近 5 秒收到 ESP 心跳或有效遥测：ESP 在线。
- STM32 超过 5 秒没有新快照：ESP 仍可在线，但 sensor/GPS 当前值全部为 `null`，前端显示 `NA`。
- 某个字段为 `null`：只显示该字段 `NA`。
- 本帧缺少整个 `sensor`/`gps` 对象或某个字段：缺少部分立即按 `null` 处理，不保留上一帧数值。
- UART CRC 错误、截断帧或重复/倒退序号不更新 STM32 数据，但 ESP 心跳仍可保持设备在线。
- 视频是否在线不参与传感器在线判定。

`age_ms` 表示 ESP 遥测链路年龄，不是传感器采样年龄。

## 11. 精确测试向量

下列 JSON 必须逐字一致；增加空格、换行或改变键顺序都会改变长度与 CRC，但接收 JSON 解析器不得依赖键顺序。

### 11.1 遥测帧

```json
{"sensor":{"gas":0,"person_detected":0},"gps":{"lat":null,"lng":null}}
```

- `type=0x01`
- `seq=1`
- JSON 长度：`70 / 0x0046`
- CRC：`0x11D8`
- 总长度：84 字节

```text
A5 5A 01 01 00 46 00 00 00 01
7B 22 73 65 6E 73 6F 72 22 3A 7B 22 67 61 73 22
3A 30 2C 22 70 65 72 73 6F 6E 5F 64 65 74 65 63
74 65 64 22 3A 30 7D 2C 22 67 70 73 22 3A 7B 22
6C 61 74 22 3A 6E 75 6C 6C 2C 22 6C 6E 67 22
3A 6E 75 6C 6C 7D 7D
11 D8 0D 0A
```

### 11.2 PAD_UP 按下

```json
{"button":"PAD_UP","state":"down","control_seq":305419896}
```

- `type=0x02`
- 外层 `seq=100`
- 内层 `control_seq=305419896 / 0x12345678`
- JSON 长度：`58 / 0x003A`
- CRC：`0x79AF`
- 总长度：72 字节

```text
A5 5A 01 02 00 3A 00 00 00 64 7B 22 62 75 74 74
6F 6E 22 3A 22 50 41 44 5F 55 50 22 2C 22 73 74
61 74 65 22 3A 22 64 6F 77 6E 22 2C 22 63 6F 6E
74 72 6F 6C 5F 73 65 71 22 3A 33 30 35 34 31 39
38 39 36 7D 79 AF 0D 0A
```

长按时每 200 ms 再发送一帧 `PAD_UP + down`，每帧必须同时使用新的
服务器 `control_seq` 和新的 ESP 外层 `seq`。

### 11.3 PAD_UP 松开

```json
{"button":"PAD_UP","state":"up","control_seq":305419897}
```

- `type=0x02`
- 外层 `seq=101`
- 内层 `control_seq=305419897 / 0x12345679`
- JSON 长度：`56 / 0x0038`
- CRC：`0x57C1`
- 总长度：70 字节

```text
A5 5A 01 02 00 38 00 00 00 65 7B 22 62 75 74 74
6F 6E 22 3A 22 50 41 44 5F 55 50 22 2C 22 73 74
61 74 65 22 3A 22 75 70 22 2C 22 63 6F 6E 74 72
6F 6C 5F 73 65 71 22 3A 33 30 35 34 31 39 38 39
37 7D 57 C1 0D 0A
```

该 `up` 通过新序号校验后立即释放前进方向。若它在网络中先于旧的
`down` 到达，ESP 和 H750 都会拒绝随后到达的旧 `down`，防止重新启动
运动。

## 12. 联调验收清单

### 12.1 遥测

1. CRC 标准自检得到 `0x29B1`。
2. 发送 11.1 测试帧，ESP 日志出现 `Complete USART telemetry frame`。
3. 服务器显示 ESP 在线，气体正常、人员未发现，GPS 为 `NA`。
4. 温湿度等字段为 `null` 时只对应字段显示 `NA`。
5. `gas:1` 产生气体报警；恢复后发送 `gas:0`，下一次报警可重新触发。
6. `person_detected:1` 产生疑似幸存者事件并截取当前云端视频帧；恢复后发送 0。
7. GPS 有效时报警事件记录同一帧经纬度。
8. CRC 错误帧、错误帧尾和只有帧头的截断帧都不更新服务器数据。
9. 重复序号不更新数据，新序号恢复更新。
10. 停止 STM32 上报超过 5 秒：ESP 仍在线，全部实时字段显示 `NA`。

### 12.2 控制（STM32 接入 `0x02` 后）

1. PAD_UP/PAD_DOWN 只映射 H750 原有运动摇杆 `RY`，分别为前进/后退。
2. PAD_LEFT/PAD_RIGHT 只映射转向摇杆 `LX`，分别为左转/右转。
3. 前或后可与左或右同时按住，H750 同时得到两个轴。
4. 前后、左右各自互斥；同轴冲突按安全停止处理。
5. 按住时每 200 ms 的新 `down` 只续命。
6. `up` 立即释放对应方向；连续 500 ms 没有新 `down` 自动释放。
7. START、L1/R1、姿态、模式和其他非四方向按钮在服务器、ESP、H750
   三层均被拒绝。
8. 坏 CRC、缺少/非整数 `control_seq` 的控制帧直接丢弃。
9. 依次注入新、旧、重复序号，只有新序号改变状态，旧包不得续命。
10. 测试 `0xFFFFFFFF -> 0` 回绕，新值 0 应被接受。
11. 实体 PS2 摇杆抢占后，云端必须先释放再重按，旧按住状态不能恢复。
12. H750 或 ESP 重启后全部方向保持释放。
13. WebSocket 断开、页面失焦、ESP 断网和 L610 断链均在 500 ms 内停车。
14. H750 不发送控制 ACK；网页不得显示“H750 已执行”。

## 13. 三端开发责任边界

| 模块 | 必须实现 |
| --- | --- |
| H750 | 保留 `0x01` 快照组帧；解析 `button/state/control_seq`；拒绝旧包；维护 500 ms 看门狗；仅向原 `velocity_cal()` 注入 RY/LX；实体 PS2 抢占；不发送控制 ACK |
| ESP32 | 校验 ESCTL 身份和四方向；拒绝旧 `control_seq`；将同一序号透传到 UART `0x02`；不等待 ACK；非运动命令不转发 |
| 云服务器 | 只接受四方向 down/up；按设备生成单调 uint32 `control_seq`；绑定并校验 device/boot/request 字段 |
| 前端 | 只显示四方向；同轴互斥、双轴可组合；按下发 down，每 200 ms 续发；松开、失焦或断连时释放全部 |

云端 `sent` 只表示已发送到ESP，不表示STM32执行。实体运动正式使用前仍应增加WebSocket鉴权。

## 14. 故障定位

ESP 当前遥测日志：

```text
Complete USART telemetry frame ...
USART frame dropped (CRC16 mismatch) ...
USART frame dropped (header received but frame timed out) ...
```

PS2版本ESP日志：

```text
PS2 UART command frame_seq=... control_seq=... button=PAD_UP state=down
```

定位顺序：

1. STM32 TX 是否输出完整 `A5 5A` 帧。
2. 长度是否为 UTF-8 字节数，是否误算了 `\0`。
3. CRC 是否从 version 开始计算。
4. 所有整数是否大端。
5. ESP 是否出现完整帧日志。
6. 服务器是否生成新的 `control_seq`。
7. ESP 日志的 `control_seq` 是否与 UDP 包相同。
8. H750 UART8 `[CTRL]` 是否接受新序号并拒绝旧序号。
9. H750 是否只把四方向转换为原有 `RY/LX`。

## 15. v5 固件构建与烧录

本次 PS2 无 ACK 固件使用独立脚本构建：

```text
build_camera_sensor_8fps_q20_ap_local_mjpeg_cloud_ps2_v5.ps1
```

脚本会在独立 staging 目录中使用 8 FPS、JPEG quality 20、AP、本地 MJPEG、云端视频和完整遥测配置，不覆盖此前 v4 产物。示例服务器地址、热点密码和 ESP-IDF 路径均为不可部署占位值；构建前应在本地配置实际参数。

`firmware_binaries`、有效 `sdkconfig` 和应用固件 SHA256 属于本地构建产物；不同部署配置会生成不同二进制，并可能固化现场地址与口令。本地构建输出目录仍为：

```text
firmware_binaries\camera_sensor_8fps_q20_ap_local_mjpeg_cloud_ps2_v5
```

烧录偏移保持不变：

| 偏移 | 文件 |
| ---: | --- |
| `0x0` | `bootloader.bin` |
| `0x9000` | `partition-table.bin` |
| `0x20000` | `usb_rndis_4g_module.bin` |

Windows 示例（先关闭占用 COM6 的串口工具）：

```powershell
. C:\path\to\esp-idf-v5.5.1\export.ps1
python -m esptool --chip esp32s3 --port COM6 --baud 460800 `
  --before default_reset --after hard_reset write_flash `
  --flash_mode dio --flash_freq 80m --flash_size 4MB `
  0x0 bootloader.bin `
  0x9000 partition-table.bin `
  0x20000 usb_rndis_4g_module.bin
```

当前 v5 已完成 ESP-IDF 5.5.1 全量构建，尚未代替用户烧录，也尚未完成 UART1 与 STM32 的实机联调。
