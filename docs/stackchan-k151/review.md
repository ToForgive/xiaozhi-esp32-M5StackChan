# StackChan K151 迁移审查与处置

审查日期：2026-10-13。目标是 K151 无遥控器版，使用 xiaozhi.me 的网络、激活、音频和 MCP。不是原厂 App 集合移植。实机固件、录像、设备均未提供，公开源码一致性不能代替视觉/硬件验收。

## 实际基线

| 来源 | 拉取的提交/引用 | 结论 |
|---|---|---|
| ToForgive fork checkout、origin/main | `e076ac496e2d93dc3a2308a0491d745afe9489ab` | 初始分支 work，干净；开发分支 feat/stackchan-k151-idf6；保留全部已有工作 |
| 78/xiaozhi-esp32 HEAD | `e076ac496e2d93dc3a2308a0491d745afe9489ab` | 与 fork 相同 |
| 小智 v2.5.2 | `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e` | 正式版；当前 fork 在其后 6 个提交，8 个文件变化；不盲目重置到 tag |
| 小智 v2.2.4 | `e77dedb1309153bb63fed285772962c920c97dd4` | 单独 detached worktree；官方补丁实际 apply --check 通过并完整应用供审查 |
| m5stack/StackChan HEAD | `1b5765599fba8aaad1811d9a79358ccc7051f5f3` | firmware/repos.json 固定 v2.2.4，主工程 1.5.1 / IDF 5.5.4 |
| m5stack/StackChan-BSP HEAD | `8d4d6fc3b7a6be379c6317c45a02a30bff8c492e` | Arduino BSP 的额外硬件证据 |
| smooth_ui_toolkit v2.12.0 | `2a18ff5d3fd6b402339d0f4f3c2834f574e3cb05` | 原厂 Avatar 的程序化绘制/动画依赖，板内裁剪 vendoring |
| mooncake_log v1.5.0 | `554d55cd8c6e551b911b99cad274a46929f05bae` | 拉取审阅；用 ESP_LOG 替代，不引入其 fmt/服务依赖 |
| ESP-IDF v6.1 | `fff9895c82d744c7237be8847347bdd1b07c6643` | 官方 tag 名是 v6.1（不是 v6.1.0）；工具报告版本在验证文档记录 |
| m5stack/m5-docs HEAD | `e5354c5dcc61d16fdc1dff9d92c2c760728d0b60` | 旧文档仓库无 K151；不能用它宣称已核对 K151 原理图 |

[逐文件索引](official-file-index.md)记录整个官方 firmware 的 SHA256 与处置分类；[补丁统计](official-patch-stat.txt)由实际应用生成。审查范围包括外置 HAL、board、Avatar、modifiers、驱动、应用入口、依赖、资源、分区，不是仓库根目录 diff。

## 官方相对 v2.2.4 的完整结构性差异

1. `xiaozhi-esp32.patch` 仅改 5 个文件：application.cc 隐藏激活码和播报，要求手机 App 绑定；assets.cc/.h 注释 EmoteStrategy；i2c_device.cc/.h 加 TryReadRegs。激活和 Emote 删除全部排除；新上游已有 display asset 接口，使用新版实现；可失败的 I2C 读取限定于 K151 驱动，避免改所有板。
2. 官方 CMake 从外部 xiaozhi/main 显式挑选旧 audio、protocol、display、application、ota 等文件，替代 main.cc。真正 K151 board 在 `firmware/main/hal/board`，不在小智 checkout 中。codec 相对 v2.2.4 CoreS3 只有 input_gain 30→60；新版 codec 在 IDF6 已适配，沿用新版，仅保留原厂增益值，实机需检查削波。
3. `main.cpp` 先 HAL，再 Mooncake launcher，安装 AiAgent/Avatar/Espnow/AppCenter/Ezdata/Dance/Setup，最后才启动小智。新移植直接走新版 app_main/Application 启动，板构造器仅初始化硬件；所有绑定、账号、World websocket、BLE App 配置、warm reboot 应用索引、secret_logic 均排除。
4. 原厂 StackChanCamera 基于 esp_video 1.3.1，额外加入 StreamCaptures 和 World 通话/快门音；新基线 esp_video ^2.4.1，HTTP 是 NetworkResult，新 EspVideo 已实现安全 JPEG/MCP。保留 DVP 接线、外部 20 MHz 时钟、GC0308 格式和镜像，使用新版 EspVideo，不复制旧上传/流媒体管线。
5. Avatar 不是 GIF：DefaultAvatar 的容器、眼睛、眼睑、嘴、speech bubble 和 spring 动画，六种 Emotion，五种装饰器及其小位图。保留原始几何、颜色、运动曲线及资源。Breath(参数amplitude=16/6.6s，实际像素位移由skin映射)、Blink(5.2s/200ms)、IdleExpression、IdleMotion、Speaking(180ms 随机开合)按原厂逻辑接入。不是音素同步。HeadPet/Imu 改为单一事件所有者和最新基础情绪恢复，防止说话、断网、睡眠时恢复旧状态。Dance/JSON远程动画控制及旧TimedEmotion捕获旧状态的恢复方式排除。
6. 原厂 display SetStatus 通过本地化文本判断 listening/standby/speaking；ShowNotification、UpdateStatusBar 是空实现。睡眠旗标未完整清除；idle expression 移除依附于 motion id，motion=0 时会残留。新桥接用真实 DeviceState，保留所有系统文字、通知、激活/升级/错误，不伪造 Thinking 状态。此次不实现独立等待回答动画；thinking/confused 仅是服务端情绪 key。
7. 原厂更新任务持 LVGL 锁后 delay，修饰器跨任务 volatile 事件旗标不能保证同步。移植把显示输入变为受锁的有界快照，硬件事件通过原子位转交，专用任务更新 Avatar；网络/语音循环不做动画等待。舵机更新在显示锁外，实际总线时延须实机量测。
8. 原厂 servo 保留 `servo/zero_pos_1`、`zero_pos_2`（460/620 回退）、原始角度十分之一度与 raw 0..1000；yaw ±128°、pitch 原厂源码 3..87°、本移植按现行产品建议收紧为 5..85°；spring speed 映射、归位和空闲 torque release。pitch 才启用反馈堵转检测（50ms采样、位置不变+电流/负载上升、连续2样本、收紧运行限位），yaw 无该保护。连续 PWM yaw 不暴露给云端。本移植额外在任一轴连续 3 次反馈失败时释放该轴扭矩并锁止到重启；仍不能把通信超时保护当成电流堵转保护，须验收。无效校准仅内存回退，避免自动覆盖 NVS。
9. 官方 partitions.csv 与新版默认布局不同：NVS 0x9000/16KiB 相同；官方 ota_0/1 各 0x4f0000，assets 0xA00000/4MiB，coredump 64KiB。采用 K151 独立同布局表，保留原厂 NVS 地址、给代码留 4.9375MiB/slot。不能靠写入 app 镜像自动迁移已有分区表；首次升级必须先备份 NVS/核对原表，禁止默认 erase-flash。官方模型 histackchan、专用 SFX 和 App 资源不复制；使用新版按用户语言/唤醒词构建的 resources，Avatar 位图编入固件。
10. 官方依赖为 mooncake v2.3.3、mooncake_log v1.5.0、smooth UI v2.12.0、ArduinoJson 7.4.2、ESP-NOW 固定 SHA，以及旧 esp-sr~2.3、LVGL~9.4、lvgl_port~2.7、esp_codec_dev~1.5.4。保留 smooth UI 所需子集和直接 IDF 驱动，排除 launcher/log/ArduinoJson/ESP-NOW，保留新版 SR 2.4.7、LVGL9.5、port2.9、codec1.6 系列。旧音频服务不重复迁入；playback_generation/播放耗尽保持上游。读取原厂idle_sec/ext_pwr/idle_lv配置，boot_ai随直接启动小智的需求不再影响入口。

## 硬件清单与映射

所有新增代码位于 `main/boards/m5stack/stackchan-k151`，除通用 OTA/NVS 的显式板配置保护外，不依赖核心中的具体板类。

| 能力/原始路径 | 处置与目标 | 证据/边界 |
|---|---|---|
| LCD + FT6336 / hal/board/stackchan.cc | 适配 stackchan_k151.cc + k151_display | 原厂 FIRMID=0x12/VENDID=0x11→ILI9342E 指令表，其余/读取失败→C；320×240；AW9523 reset |
| 音频 / hal/board/cores3_audio_codec.* | 新版 codec + 原厂 gain=60 | ES7210/AW88298，I2S 0/33/34/14/13；不将 GPIO0 当按钮 |
| 摄像头 / stackchan_camera.* | 新版 EspVideo | DVP 39/40/41/42/15/16/48/47，VSYNC46/HREF38/PCLK45；XCLK NC 外置晶振 |
| AXP2101、AW9523 | 原厂板初始化 +新版公共基类 | I2C SDA12/SCL11；电池、充电、背光；无充电独立动画承诺 |
| PY32 / hal_io_expander.cpp + driver | 保留协议，适配 HAL | 0x6f，IO0 VM_EN、IO13 RGB、12灯；独立供电命令/休眠策略 |
| SCS / hal_servo.cpp + FTServo | 适配原生 IDF串口 | UART1 TX6/RX7/1Mbps；限位、速度、home、pitch反馈保护、通信丢失锁止、校准键 |
| Si12T / hal_head_touch.cpp | 保留 driver +手势算法 | 3区强度/加权位置，40%滑动阈值；成功读取才产生事件 |
| BMI270 / hal_imu.cpp | Bosch baseline driver +检测 | I2C0x69，shake阈值16m/s²，拾起原厂发射代码已注释，不能声称原有拾起反馈 |
| PCF8563 / hal_rtc.cpp | 保留 driver + RTC读取MCP | BM8563/PCF8563兼容寄存器，0x51；不更改官方后台时区语义，避免未经校验 RTC 覆盖系统时钟 |
| IR / BSP examples/IR | 新增 IDF RMT 基础 TX/RX | GPIO5/10；NEC+raw、有限capture/学习存储与重放；不是完整 IRremoteESP8266 协议库或原厂学习 App |
| INA226 / BSP M5StackChan.cpp | 新增基础电压/电流读取 | 0x41，0.01Ω/8.19A，额外遥测；不替代 AXP 电量百分比 |
| NFC / BSP examples/NFC | 待迁移，不伪称完成 | ST25R3916，0x50；原厂 firmware 无驱动；示例依赖 M5UnitUnifiedNFC Arduino，需独立 IDF驱动和协议验证 |
| LTR553 / BMM150 | 盘点、暂未迁移 | 接近/环境光及磁力计存在于 CoreS3；公开原厂 firmware 无对应行为实现，本移植不宣称9轴姿态或环境感应已完成 |
| 激光 / hal_espnow.cpp | 排除云端激光控制 | GPIO2 在原理图是 Grove A SDA；原厂 ESP-NOW服务可用来控制外接激光，本移植不驱动该脚 |
| SD/扩展口/USB/RTC中断 | 盘点，按上游USB串口/接口保留 | SD与LCD共享SPI线，需要协调GPIO35方向和片选；未初始化SD/存储应用，USB透传与扩展口保持可用 |
| World、EzData、账号、AppCenter、Dance、BLE App、ESP-NOW | 排除 | 用户明确不需要；无手机绑定和模型API key；不部署 Pi/HA/网易云，MCP工具是后续设备→局域网接口扩展点 |

## 引脚和证据限制

代码/BSP之间 GPIO5/10(IR)、6/7(SCS)无交集；LCD SPI3 3/35/36/37，音频0/13/14/33/34，DVP15/16/38..42/45..48，I2C11/12；USB19/20保留。CoreS3 GPIO0同时是音频 MCLK，不能借用普通 boot_button。CoreS3 microSD为SPI共享总线（不是IR/SCS引脚），GPIO35同时用作当前LCD D/C与SD MISO，需要专门协调方向/事务；本次不启用SD。

Shell直接下载官方站点/PDF返回403；浏览工具成功读取 [K151规格](https://docs.m5stack.com/en/StackChan)及全部5张原理图的文字/网名。已核对关键部件、地址、引脚与公开代码，未把截图工具未返回的图像描述为视觉检查。对应批次仍需实机确认：

- [CoreS3](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/490/Sch_M5_CoreS3_v1.0.pdf)：Quad PSRAM、SPI/SD共享、音频MCLK、外置摄像头晶振、AXP/AW9523/BM8563。
- [Power](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/SCH_Power.pdf)：PY32地址0x6f、VM_EN/RGB、SCS电平转换与VM供电、INA226地址0x41和10mΩ分流、CoreS3总线5/10与6/7。IR引脚方向另由BSP收发示例交叉确认。
- [Touch](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/SCH_Touch.pdf)：Si12T地址0x68、IRM56384、12颗RGB、ST25R3916地址0x50。
- [Adapter](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/SCH_Adapter.pdf)、[Ring](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1205/SCH_Ring.pdf)：电池保护、舵机接口、USB数据/供电转接；均无MCU固件需要恢复。

现行K151产品建议俯仰5..85°，因此本移植收紧原厂代码3..87°。不能假设所有旧批次与这些PDF完全一致。


## OTA、持久化和服务

独立 type/name `m5stack-stackchan-k151`。配置/激活服务使用构建CONFIG_OTA_URL（默认官方），忽略旧wifi/ota_url而不删除该键。默认在应用升级入口即拒绝自动及手动网络固件更新，保持对话；OTA图像头改为只累计新读入字节，避免短读重复头数据。保留 xiaozhi.me 的激活/协议配置请求；可显式开启 K151 OTA。启用时在写 OTA 分区前核对新镜像 app_desc.project_name 与独立 K151 项目名，拒绝普通 xiaozhi/CoreS3 镜像；项目名不是密码学签名，生产可信更新仍需签名/控制下载渠道。原厂 NVS全清除与应用工厂重置不迁入；K151 NVS初始化失败不能自动清空校准。

## 复现审查

```sh
git fetch https://github.com/78/xiaozhi-esp32.git refs/tags/v2.2.4:refs/tags/v2.2.4 refs/tags/v2.5.2:refs/tags/v2.5.2
git worktree add --detach ../xiaozhi-v224-reference v2.2.4
git clone https://github.com/m5stack/StackChan.git ../StackChan-reference
git -C ../StackChan-reference checkout 1b5765599fba8aaad1811d9a79358ccc7051f5f3
git -C ../xiaozhi-v224-reference apply --check ../StackChan-reference/firmware/patches/xiaozhi-esp32.patch
git -C ../xiaozhi-v224-reference apply ../StackChan-reference/firmware/patches/xiaozhi-esp32.patch
```

最终实现和测试边界见 README 与 validation.md；审查决策不等于编译或实机通过。
