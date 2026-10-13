# M5Stack StackChan K151

最新本机接续、已修复缺陷和未完成项见 [HANDOFF.md](../../../../docs/stackchan-k151/HANDOFF.md)。完整固件仍未链接；不把分离编译当成可刷写成品。

这是原厂 K151 无遥控版的小智板型。开发基线为 fork `e076ac49`（v2.5.2 后6个提交），ESP-IDF 6.1。独立 board type/name 是 `m5stack-stackchan-k151`，镜像 project name 是 `xiaozhi-k151`。当前交付已实现代码与目标架构分离编译；完整固件构建被官方组件注册表连接阻断，未刷机、未实机验证。

## 实现

- 原厂 DefaultAvatar 的全部程序化几何/颜色/位图、六种情绪、五种装饰，保留呼吸、眨眼、随机眼神/头部动作。不是GIF替代。
- 说话嘴部每180ms随机开合，不是音素同步。状态跟随新版真实 DeviceState及播放耗尽处理；没有独立Thinking状态。
- 三区摸头滑动→开心/爱心/腮红及原厂随机抬头/歪头动作，松手3秒后恢复最新基础情绪，丢失松手事件时最长30秒回退。摇晃→眩晕/腮红/嘴部摆动及归位；取消或结束后恢复眼睛、锁、嘴部和当前说话状态。
- ILI9342C/E识别、FT6336触摸、ES7210/AW88298音频、GC0308摄像头、新版EspVideo/MCP、AXP电池/背光、PY32供电/RGB、SCS舵机、Si12T、BMI270、RTC读取、INA226电压/电流。
- 原生IDF RMT红外NEC/raw收发，4个有界学习槽位与重放；这是新增基础实现，未获得/恢复原厂完整学习App，也不是完整IRremoteESP8266协议集。
- 状态/系统/通知文字使用小智字体，激活码、连接、升级、错误提示保留。照片预览接入新版显示接口。表情在独立任务更新，UART/I2C执行位于显示锁外；启动UI只创建一次。

详细差异、排除项和依据见 [审查与移植映射](../../../../docs/stackchan-k151/review.md)。NFC、LTR553、BMM150、SD存储应用尚未实现；磁力计不等于BMI270已支持的加速度/陀螺仪。

## 构建

在可访问 Espressif Component Registry 的机器中，使用独立checkout/分支：

```sh
export IDF_TOOLS_PATH=/path/to/idf-tools
source /path/to/esp-idf/export.sh
idf.py --version  # 本次核验输出 ESP-IDF v6.1
python3 scripts/build.py m5stack/stackchan-k151 \
  --name m5stack-stackchan-k151 --language zh-CN --wake-word nihaoxiaozhi
```

不要用 CoreS3 板名构建。当前官方后台的激活、协议地址下发和认证使用上游机制，不需要把模型API key写入设备；不依赖M5Stack账号/World/AppCenter。为避免旧App路由影响，K151忽略NVS里的wifi/ota_url，使用构建的CONFIG_OTA_URL（默认官方端点）；原键不删除。独立板型能否正常取得官方后台激活配置仍须联网实机验证。

官方组件服务器受限时可运行分离编译检查（Git依赖固定于文档锁文件；输出是静态库）：

```sh
python3 scripts/check_k151_native.py --reference-root /tmp/k151-refs \
  --work-dir /tmp/k151-compile --fetch
python3 scripts/check_k151_native.py --reference-root /tmp/k151-refs \
  --work-dir /tmp/k151-compile --allow-ota
python3 -m unittest discover -s scripts/tests -v
```

这个检查真实编译ESP32-S3代码、LVGL9.5和板级/OTA/NVS改动，**不会链接小智完整固件或生成可刷写镜像**。完整构建、资源大小和实机结果不能由它推断。

## 持久化、分区与OTA

保留原厂 `servo/zero_pos_1`、`zero_pos_2`，读取现有 `xiaozhi/idle_sec`、`ext_pwr`、`idle_lv`；新设备默认不自动关机、动作等级2。无效/缺失时仅内存使用460/620回退，明确校准操作才写NVS。K151遇到NVS损坏不自动清空。分区表沿用原厂NVS地址0x9000/16KiB、双OTA槽位各0x4f0000、assets 0xA00000/4MiB、coredump 64KiB。不要默认erase-flash；原厂分区布局/校准应先备份核对，固件app更新不会自行迁移分区表。本任务不含刷机。

默认禁用固件网络更新，保留官方激活/配置请求。要显式开启，先完成canonical配置，再在 `idf.py menuconfig` 勾选 `K151_ALLOW_OTA`，然后 `idf.py build`。重新执行canonical build脚本会恢复默认关闭。即使后台返回force标记，默认策略仍拒绝升级；启用后，在写OTA分区前检查项目名 `xiaozhi-k151`，拒绝普通CoreS3/小智镜像。项目名校验不是签名机制；兼容K151镜像的来源仍应受控。

## 舵机边界

- yaw限制±128°，pitch按现行产品建议收紧为5..85°。输入角度和原厂动画均为十分之一度；`speed`是spring响应参数，不是°/秒。MCP最高500/1000，归位pitch会钳制到安全下限。
- GPIO6/7、UART1、1Mbps；PY32 IO0控制供电。空闲自动释放扭矩，睡眠发起归位，完成或达到5秒截止后停止供电；恢复后给电源200ms启动时间。实际到位与断电时序需要实机核对。
- 保留原厂仅pitch启用的反馈堵转检测：位置变化很小且电流/负载异常，连续2个样本后停止并收紧该方向运行限位。yaw没有电流堵转保护。
- 新增任一轴连续3次反馈失败的通信锁止/扭矩释放，重启后才恢复。反馈采样约50ms，但任务/总线负载会影响实际间隔；不能宣称覆盖全部机械故障。
- 不向云端暴露无限连续旋转、清空校准、激光控制。未做带负载、堵转、温升、掉电测试。

## MCP与扩展点

`self.robot.get_capabilities`报告实际初始化情况；可用时注册`move/home/servo_power/rgb/power_read/rtc_read`和`ir_send_nec/ir_send_raw/ir_capture/ir_learn_last/ir_replay`。控制工具返回排队接受情况，不表示动作已完成；保护/临时反馈可能拒绝实际动作。硬件校准方法保留但不向对话模型直接开放。

红外接收最多256个时长，发送帧最长300ms、载波30..60kHz、NEC默认38kHz。接收器不能测出载波频率，学习时38kHz是默认假设，支持4个`k151_ir/v1_slot0..3`的NVS槽位；超长/溢出帧拒绝保存。接收结束间隔12ms，许多空调长协议需要后续专门适配。`ir_capture`可检查异步存储/发送的完成结果。

后续可在设备MCP工具中调用局域网HTTP接口接入HA/网易云；工具选择仍由云端模型决定。这次没有Pi部署、令牌/账号处理、硬编码凭据或自部署语音服务端。

验证、预算、许可证和实机清单见 [validation.md](../../../../docs/stackchan-k151/validation.md) 与 [THIRD_PARTY.md](THIRD_PARTY.md)。
