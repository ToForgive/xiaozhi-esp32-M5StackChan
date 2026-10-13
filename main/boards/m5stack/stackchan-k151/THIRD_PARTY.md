# 来源与许可证

此目录包含真实原厂源码和新的K151桥接代码，保留原作者文件头。根项目MIT许可证保持不变。

| 导入来源 | 固定SHA / 版本 | 范围 | 必须保留 |
|---|---|---|---|
| m5stack/StackChan | 1b5765599fba8aaad1811d9a79358ccc7051f5f3 | Avatar/装饰/小位图、基础修饰器、motion、Si12T/PY32/RTC/FTServo/BMI wrapper、板级参考 | 各文件M5Stack 2026 SPDX及 [LICENSE-M5Stack](LICENSE-M5Stack)（MIT） |
| FTServo（官方工程中的IDF适配） | 同上；原库版权2024 ftservo | SCS串口协议/反馈 | [FTServo LICENSE](hal/drivers/FTServo_Arduino/LICENSE)（MIT）；名字含Arduino但此拷贝串口层实际使用IDF，不链接Arduino框架 |
| Bosch BMI270 SensorAPI | 官方工程内baseline文件，同上 | bmi2/bmi270（排除legacy/context/FIFO等重复配置） | [Bosch LICENSE](hal/drivers/bmi270/BMI270_SensorAPI/LICENSE)（BSD-3-Clause）和文件头 |
| Forairaaaaa/smooth_ui_toolkit | 2a18ff5d3fd6b402339d0f4f3c2834f574e3cb05 / v2.12.0 | core、tools、LVGL wrapper、widget头/支持；排除games和未使用fpm | [uitk LICENSE](vendor/uitk/LICENSE)（MIT）和作者/引用文件头 |
| 当前fork CoreS3 codec | e076ac496e2d93dc3a2308a0491d745afe9489ab | IDF6版ES7210/AW88298 wrapper，仅gain调整 | 根项目MIT许可证 |

本板继续依赖上游manifest中的LVGL、esp_lvgl_port、esp_codec_dev、esp-sr、esp_video、网络/字体/资源等组件。已检查的核心许可：LVGL MIT、esp_lvgl_port/esp_codec_dev Apache-2.0、ESP-SR为ESPRESSIF MIT（限ESPRESSIF产品的许可），二进制blob及字体声明须按实际组件发行文件保留。字体Git参考没有独立LICENSE文件，未据此推断所有生成字体的授权范围。完整转移依赖解析被网络阻断，**未完成所有解析后传递依赖的许可证清单**，发布前必须保留实际managed_components中的LICENSE/NOTICE及字体声明，不能仅附本表。

K151导入的PCF8563/PY32驱动此次增加40ms事务超时、RTC供电失效拒绝和可返回错误的VM位写入；不再声称这两份驱动与原厂字节相同。Avatar来源25/25字节一致的记录保持有效。

IR RMT发送/接收、NEC时长生成/解码、学习存储是本移植新增MIT代码，依据IDF原生API和原厂BSP引脚示例；未复制IRremoteESP8266或原厂IR App实现。ArduinoJson、ESP-NOW、Mooncake及其App资源没有导入。原厂SFX、Launcher图片/动画、World服务/账号凭据未导入。

[完整来源审查](../../../../docs/stackchan-k151/review.md)及[官方文件SHA256索引](../../../../docs/stackchan-k151/official-file-index.md)可用于追溯。分离编译的额外官方Git参考版本见native-reference-lock.json；它们是验证依赖，不作为源码拷贝进入此目录。
