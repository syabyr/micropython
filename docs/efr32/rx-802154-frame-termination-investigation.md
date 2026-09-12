# EFR32MG21 802.15.4 RX 帧不终止问题 — 调查总结

> 日期: 2026-09-12
> **状态: 已解决 (2026-09-13)。根因: RAIL 2.19.3 librail 缺陷；换用 GSDK 3.1.1
> 的 librail (RAIL 2.11.3) 后 RX 完全正常。详见第 8 节。**
> 分支: `v1.29.0-efr32mg21`
> 目标硬件: EFR32MG21 Breakout Board REV 1.0 (EFR32MG21B010F1024IM32)
> 相关代码: `ports/efr32mg21/radio.c`, `ports/efr32mg21/rail/` (现使用 GSDK 3.1.1 librail)
> SDK 约束: 尽量使用 simplicity-sdk 里的库 (外置 `~/develop/efr32/simplicity-sdk`, 软链为 `lib/simplicity-sdk`)

---

## 1. 当前移植状态 (Port State)

| 模块 | 状态 |
|---|---|
| Console REPL (USART0) | ✅ 工作 |
| LED / 按键 | ✅ 工作 |
| SPI-flash VFS (W25Q80D, littlefs2) | ✅ 工作 |
| I2C (`machine.I2C`, SI7021) | ✅ 工作 |
| RAIL 802.15.4 **TX** (250 kbps OQPSK) | ✅ 工作 (抓包器可收到带正确长度/FCS 的帧) |
| RAIL 802.15.4 **RX** | ❌ **帧不终止** (本次调查的对象) |

构建: `make BOARD=efr32mg21_devboard` 干净通过, 固件约 193 KB flash, 硬浮点 (`-mfloat-abi=hard -mfpu=fpv5-sp-d16`), 通过 JLink `make flash` 烧录。

---

## 2. RX 故障的精确特征 (Symptom)

帧**能通过地址/格式过滤**, 但 FRC (Frame Controller) **永远不终止帧**。

诊断函数 (MP 模块 `radio` 内建):
- `radio.stats()` → `(preamble, sync1, sync2, rx_packet, frame_error, fifo_overflow, rx_aborted, addr_filtered, cal_needed, tx_completion, last_packet_status)`
- `radio.incoming()` → `(filter_passed, incoming_bytes, incoming_status)` (status: 8=RECEIVING)
- `radio.peek()` → `(packetBytes, packetStatus, fifoAvail, bytes)`

在 CH15、正确天线 (path 1) 下的典型快照:

```
stats    = (2014, 31, 3, 0, 0, 0, 0, 0, 1, 0, 0)
incoming = (30, 2, 8)         # 30 帧通过过滤, 但 status 一直是 RECEIVING(8)
peek     = (512, 8, 512, b'\x53\x41\x88\xb4\x5d\xd4\xff\xff\x8d\x0e\x09\x12\xfc\xff...')
```

关键点:
- `sync1=31` — 同步码已检测到 (前导码/同步都没问题)。
- `filter_passed=30` — 帧头解析成功, 通过了 802.15.4 地址/帧过滤。
- `rx_packet=0`, `frame_error=0` — **既没有 RX_PACKET_RECEIVED, 也没有 RX_FRAME_ERROR**。帧既不成功也不失败, 就是"挂起"。
- RX FIFO 被填满到正好 **512 字节** (内部 RX FIFO 的满容量)。
- `peek` 的 `status=8` (RECEIVING) — RAIL 认为这一帧还在接收中。
- `peek` 的字节内容开头正确: `0x53`(长度字节 = 83)、`0x41 0x88`(FCF)、随后是地址和载荷 — 与抓包器抓到的 PSDU 一致。

**结论: 解调、同步、帧头解析、过滤全部正常, 唯一异常是 FRC 没有按长度字节终止帧, 而是持续写入直到 FIFO 满。**

---

## 3. 已逐一排除的根因 (Findings)

### 3.1 配置数组 100% 正确 ✅
`ieee802154_efr32xg21_configurator_out.c` (901 行) 与外置 SDK 官方副本逐字节 diff, **零差异**。

关键 FRC 寄存器 (来自该配置):
| 寄存器 | 地址 | 值 | 含义 |
|---|---|---|---|
| DFLCTRL | 0x400C | `0x00148001` | DFLMODE=1 (SINGLEBYTE), DFLBITS=8, DFLINCLUDECRC=1 |
| MAXLENGTH | 0x4010 | `0x407F` | MAXLENGTH=127, INILENGTH=4 |
| CTRL | 0x4048 | `0x7A0` | TXFCDMODE=2, RXFCDMODE=2, BITSPERWORD=7 |
| FCD0 | 0x40B4 | `0x4000` | frame descriptor 0 |
| FCD1 (TX) | 0x40B8 | `0x4CFF` | |
| FCD2 | 0x40BC | `0x4100` | |
| FCD3 (RX) | 0x40C0 | `0x4DFF` | |

其中 `DFLMODE=1`(单字节长度)是 TX/RX 共用的 — 这正是 RX 终止帧所依赖的机制。

### 3.2 PHY 绑定正确 ✅
`radio.c:129` 绑定 `RAIL_IEEE802154_Phy2p4GHz = &ieee802154_2p4_antdiv_channelConfig`, 为标准 250 kbps OQPSK 配置 (头文件 `RAIL0_IEEE802154_2P4_ANTDIV_PROFILE_IEEE802154OQPSK`)。

### 3.3 校准 (Calibration) 不是问题 ✅
- `RAIL_CAL_ALL` 与 `RAIL_CAL_TEMP | RAIL_CAL_ONETIME` **字面相等** (rail_types.h 定义), 不存在"掩码选错"的可能。
- 手动 `RAIL_Calibrate(TEMP_VCO|RX_IRCAL)` 提前强制校准被观察到**反而更糟** (产生垃圾 sync 锁、频率偏移不稳)。已改为惰性校准 (`RAIL_ConfigCal` + `RAIL_EVENT_CAL_NEEDED`)。
- 当前 `cal_needed=1` 且惰性校准已生效 (TX 正常证明 VCO 已锁定), 校准非根因。

### 3.4 `RAIL_ConfigMultiTimer` 不是必需 ✅
rail.h 文档明确: "It is not necessary to call this function if the MultiTimer APIs are not used." 软件定时器复用, 与基本 RX 无关。

### 3.5 初始化顺序已多种组合尝试 ✅ (均未修复)
1. `RAIL_IEEE802154_Init` → `Config2p4GHzRadio` (OpenThread/官方顺序)
2. `Config2p4GHzRadio` → `Init`
3. `RAIL_ConfigCal` → `Config2p4GHzRadio` → `Init` (Series 1 sniffer-tradfri 顺序, **最后一次尝试**)

当前顺序 (radio.c:434-491): `ConfigData → ConfigCal → Config2p4GHzRadio → Init → ConfigEvents → ConfigTxPower/SetTxPower → SetLong/ShortAddress → SetPanId → SetTxFifo → PauseRxAutoAck(false) → radio_channel_set()`。

### 3.6 天线路径问题 (独立 bug, 已定位但非根因) ⚠️
- 天线接在 **RF2G4_IO2** (引脚 12), 即 `RAIL_ANTENNA_1` (path 1)。
- 但代码默认 `defaultPath = RAIL_ANTENNA_0` (path 0)。
- `radio.antenna(1)` 切换后信号强度显著提升 (抓包器 23 帧 vs 2 帧, preamble 2014 vs 768)。
- **但这只改善信号强度, 帧不终止的问题依旧** — 天线是独立问题, 不是帧终止根因。
- 待办: 默认路径应改为 `RAIL_ANTENNA_1` (低风险, 但单独改它不会修复 RX)。

### 3.7 librail 版本差异 (未决线索) ⚠️
发现**两个不同**的预编译 librail:

| 来源 | 大小 | md5 |
|---|---|---|
| 本 port 使用 (`ports/efr32mg21/rail/`) | 1518556 | `be9342ec…` |
| 外置 simplicity-sdk (`~/develop/efr32/simplicity-sdk/…`) | 1518556 | `be9342ec…` (相同) |
| 树内 gecko_sdk (`lib/gecko_sdk/…`) | 1458598 | `5dfca833…` (不同/更旧) |

本 port 用的是**外置 simplicity-sdk 版本** (与树内 gecko_sdk 版本不同)。树内 gecko_sdk 版本此前已按用户要求试过并排除。运行时 RAIL 版本 = `2.19.3`。

---

## 4. 核心未解悖论 (The Unresolved Paradox)

> 配置正确 (DFLMODE=1 为 TX/RX 共用), TX 正常工作 (证明 DFLMODE 已生效), 但 RX 永不终止。

- 若 DFLMODE 未生效: TX 会因固定长度 0 而发不出有效帧。但 TX 正常 → **DFLMODE 已生效**。
- 若 DFLMODE 已生效: RX 应读到长度字节 (0x53=83) 并在 83+CRC 字节后终止。但实际**填满 512 字节不终止** → 行为等价于 DFLMODE="无限长度"模式。

两者矛盾。可能的解释方向:
1. **RAIL 在 RX 路径上用另一套长度来源覆盖了 DFL** (例如某 Series 2 特有的 RX 长度寄存器/模式), 而该来源被错误地置为"无限"。
2. 配置虽正确, 但**某个 RX 专属步骤缺失**, 导致 RAIL 内部状态机没进入"按长度终止"的 RX 路径。

---

## 5. 尚未尝试 / 建议下一步 (Remaining Leads)

按优先级排序:

1. **`RAIL_SetTxTransitions` / `RAIL_SetRxTransitions`** — 官方 SDK 完整初始化 (sl_rail_util_init.c.jinja) 明确调用, 而本 port **没有**。这是与官方初始化的唯一明显差异。虽直觉上与"长度终止"无关, 但值得排除 (影响 RF 状态机, 可能间接影响 FRC 收尾)。

2. **`RAIL_IEEE802154_Config2p4GHzRadioAntDiv`** — 本 port 用 `_antdiv_channelConfig` 绑定到了普通 `Config2p4GHzRadio`。官方 `sl_rail_util_protocol.c` 对 antdiv 变体用的是 `RAIL_IEEE802154_Config2p4GHzRadioAntDiv`。改用专用 antdiv 入口可能触发不同的 RX 配置路径。

3. **对照一个官方最小 RX 例程** — 用 SDK 自带的 `sl_rail_test` 154_rx (或 railtest) 在 xg21 上编译运行, 确认同一块 librail 在官方工程里 RX 是好的。若官方例程也 RX 失败, 则问题在 librail/硬件; 若成功, 则问题在本 port 的某个遗漏调用。

4. **librail 版本** — 尝试更新/更早的 RAIL 库版本 (当前 2.19.3), 排除特定版本 bug。

5. **修复天线默认路径** (独立, 低风险): `defaultPath = RAIL_ANTENNA_1`。

---

## 6. 复现方法 (Reproduction)

```python
import radio
radio.init()          # 打印 rail 版本 / cfgdata / cfgcal / ieee_cfg2p4 / ieee_init 返回值
radio.channel(15)
radio.antenna(1)      # 切到正确天线 path 1 (RF2G4_IO2)
radio.stats()         # 看 rx_packet / frame_error
radio.incoming()      # 看 filter_passed 与 status
radio.peek()          # 看 packetStatus(8=RECEIVING) 与 fifoAvail
```

需要另一台 802.15.4 设备在 CH15 持续发包 (验证时用了 TI CC2531 sniffer + tradfri 网络实测流量)。

---

## 7. 一句话总结

RX 链路的前半段 (解调 → 同步 → 帧头解析 → 过滤) 全部正常, 唯一断点在 **FRC 收尾**: 帧头长度字节被正确读出并写入 FIFO, 但 FRC 没有据此终止帧, 而是持续接收直到 FIFO 满 (512 字节)。配置数组与官方逐字节一致、DFLMODE=1 已由 TX 证明生效, 因此问题指向**某个 Series 2 RAIL RX 专属的缺失调用或长度来源覆盖**, 而非配置本身。

---

## 8. 突破：同款芯片工作参考对照 (2025-09-12)

用户提供了 `/Users/mybays/develop/efr32/Sniffer_802.15.4_SONOFF_USB_Dongle_Plus_E` ——
一个在**同款芯片 EFR32MG21** (SONOFF Zigbee 3.0 USB Dongle Plus E) 上**RX 完全正常**的
802.15.4 sniffer 开源工程 (MIT)。逐项对比后取得决定性证据。

### 8.1 sniffer 的初始化流程 (Hal_Radio.c)

```c
RAIL_Init(&railCfg, NULL);
RAIL_IEEE802154_Config2p4GHzRadio(gRailHandle);   // 标准入口，不覆盖任何弱符号
RAIL_IEEE802154_Init(gRailHandle, &rail154Config); // ackConfig.enable = false
// 之后:
RAIL_ConfigRxOptions(STORE_CRC);
RAIL_IEEE802154_SetPromiscuousMode(true);
RAIL_IEEE802154_AcceptFrames(STANDARD|ACK);
RAIL_ConfigEvents(...);
RAIL_ConfigAntenna(defaultPath = RAIL_ANTENNA_1);  // 路径 1!
RAIL_SetTxFifo(...);
RAIL_StartRx(channel 11);
```

注意：sniffer **没有** RAIL_ConfigData / RAIL_ConfigCal 调用（用库默认值），
也**没有**在事件回调里调 RAIL_GetRssi。

### 8.2 决定性差异：PHY profile 绑定

提取 sniffer 的 librail (GSDK 3.1.1, RAIL 2.7.x) 并解析重定位表：

| 入口函数 | sniffer (2.7.x) 解析结果 | port 当前状态 (2.19.3) |
|---|---|---|
| `RAIL_IEEE802154_Config2p4GHzRadio` | channelConfig = base + **STD delta** (`3fd0af81`) | **被 port 覆盖为 antdiv channelConfig** (base + antdiv delta `804a208e`) |
| `RAIL_IEEE802154_Config2p4GHzRadioAntDiv` | channelConfig = base + ANTDIV delta (`804a208e`) | 未使用 |

port 自己的 librail 2.19.3 中 `rfhal_standard_phys.o` 的弱符号
`RAIL_IEEE802154_Phy2p4GHz` 同样重定位到 STD channelConfig (`sli_rail_85fc3dfd`)。

**结论：同款芯片上正常工作的 sniffer 用的是标准 (STD) 250 kbps profile；
本 port 却把 ANTDIV profile 绑进了标准入口。** 这是与工作参考之间唯一发现的
寄存器状态差异。antdiv delta 启用 AGC 天线分集位 (AGC.CTRL0 PWRTARGET=0xF5 vs
0x14、GAINRANGE=0x0C304187 vs 0x04304187、MODEM.CTRL1=0x0052C007、
TIMING=0x08A0014B、DIGIGAINCTRL=0、额外 SYNTH LPFCTRL 段)。
MG21 双路内部 RF path + AGC antenna diversity 位开启但无外部天线切换 GPIO 的
组合，硬件上可能在两路内部 RF path 间来回切换导致解调无法锁定帧尾。

### 8.3 两版 librail 的 antdiv delta 差异 (旁证)

- 2.7.x antdiv delta = 204 字节；2.19.3 antdiv delta = 240 字节 (新增尾部
  SYNTH.LPFCTRL1RX/TX、LPFCTRL2RX/TX AND/OR、DSMCTRLRX 一段)。
- 底层语义相同：均为 antdiv 变体。

### 8.4 修复过程与最终结果 (2025-09-13)

按上述方案改用 STD profile (删除强符号覆盖) 后**仍然失败** —— 症状完全相同
(status=8 RECEIVING、FIFO 满 512、无 RX_PACKET_RECEIVED)。**排除 antdiv 假设**。

随后按用户建议，将 librail 整体替换为 sniffer 工程自带的 GSDK 3.1.1 版本
(报号 rail=2.11.3 build 1，即 RAIL 2.11.x):

- 替换 `ports/efr32mg21/rail/` 下的 librail + 全套头文件 (rail.h/rail_types.h/
  rail_features.h/rail_assert_error_codes.h/rail_chip_specific.h/
  protocol/ieee802154/rail_ieee802154.h)
- 从构建中移除外部的 `ieee802154_efr32xg21_configurator_out.c` (2.7x 库内部
  自带完整 configurator 数据，弱符号绑定直接编死为 STD profile)

**结果：RX 立即完全恢复正常。**

| 验证项 | 结果 |
|---|---|
| radio.stats() rx_packet | 30 秒内 0→53，两分钟 148→423 持续增长，无卡死 |
| frame_error | ~25% (正常射频环境的 CRC 坏帧统计) |
| radio.fifo() | 0 (持续排空，不再积压 512 字节) |
| radio.rx() | 返回完整 802.15.4 帧 (0x41 0x88... Data+ack-req+PAN-compress) |
| RSSI | -331 quarter-dBm ≈ -82.8 dBm (正常) |
| TX | 'sent' (CSMA/CCA 正常) |
| last_packet_status | 7 = RAIL_RX_PACKET_READY_SUCCESS |

### 8.5 最终根因

**RAIL 2.19.3 的 librail_efr32xg21_gcc_release.a 在 EFR32MG21 上存在 RX 帧终止
缺陷**（或与本 port 的组合不兼容）。port 的初始化流程、寄存器配置、事件处理
均正确 —— 同样的代码仅替换 librail 为 RAIL 2.11.3 即恢复正常。

症状回顾（仅 2.19.3 出现）：帧头通过地址过滤 (RX_FILTER_PASSED)、长度字节
正确解析并写入 FIFO，但 FRC 永不终止帧；RX FIFO 填满 512 字节拼接多个帧；
radio 卡死在 RECEIVING，之后 preamble/sync 计数也不再增长；无
RX_PACKET_RECEIVED / RX_FRAME_ERROR / RX_FIFO_OVERFLOW 事件。

注：port 中 radio.c 之前把 antdiv channelConfig 绑到标准入口的历史问题
也一并修复了 (删除覆盖 + 库默认 STD profile + SetRx/TxTransitions +
天线默认路径 1 + 关闭 autoack)，这些修改在 2.11.3 下工作良好，予以保留。

### 8.6 遗留事项

- `radio.ircal()` 在中断回调上下文外手动调 RAIL_CalibrateIrAlt 会触发
  HardFault (Bus=20018204)，尚待调查；lazy calibration 路径正常。
- 若未来需要回到新版 RAIL，可尝试 Simplicity SDK 更新版本的 librail
  (如 GSDK 4.x 自带)，但必须先在真机上验证 RX 帧终止。
