# 验证、预算和待验收项

本交付是已落地的源码迁移。完整固件链接、设备刷写、视觉对比及硬件验收尚未完成，不提供可刷写成品声明。

## 实际执行结果

| 检查 | 结果 | 证据/边界 |
|---|---|---|
| fork/upstream/官方基线核验 | 完成 | review.md及source-shas.tsv；保持e076ac49基线，没有降级/合并 |
| 官方v2.2.4补丁 | apply --check及实际apply通过 | 独立worktree，official-patch-stat.txt；没有把隐藏激活码的修改带回fork |
| 原厂Avatar来源 | 25/25文件字节相同 | evidence/avatar-source-equality.tsv：default skin、核心元素、全部装饰/位图；这证明公开源码保存，不证明实机观感一致 |
| 宿主测试 | 121项全部通过，无跳过；见evidence/host-tests.log | 编译运行实际Model/NEC代码、OTA入口/分片头读取语句，及仓库MCP真实解析/响应测试；平台对象按测试框架替代 |
| ESP-IDF6.1目标架构编译 | 默认关闭OTA及显式开启两种配置均编成libmain.a | evidence/native-default.log、native-ota-enabled.log；真实ESP32-S3/GCC15.2、LVGL9.5、官方依赖头，不使用伪造的IDF/驱动API头；未链接整机固件 |
| 完整canonical K151构建 | **未通过依赖解析阶段** | evidence/full-build-blocked.log；https://components-file.espressif.com/components/78/esp-ml307.json连接失败，单独只读探测返回代理403。尚未进入完整工程源码编译/链接 |
| 格式/差异检查 | 完成 | 21个新增/改写C++文件clang-format通过；CRLF保留，6个原样FTServo文件有47处原厂行尾空白，其余改动git diff HEAD --check通过；详见evidence/whitespace-check.txt |
| 实机 | 未执行 | 无设备、现用固件及录像；没有刷写、上电测试、远程部署、合并或Release |

安装环境最初被只读用户cache路径及dl.espressif.com下载403阻断：cache移至workspace；官方idf_tools提供的无版本constraints安装选项配合PyPI完成Python环境，IDF依赖检查可通过。工具链包仍由官方安装器校验，未关闭TLS/镜像签名或下载包校验。完整工程没有用伪造registry元数据绕过依赖解析；分离编译明确禁用component manager并只验证锁定Git源码的目标编译。

分离检查编译整个K151源码（含板型、codec、bridge、Avatar、HAL、IR和选定vendor代码）、改动的ota.cc/main.cc。application.cc的新增升级入口守卫另由宿主测试编译真实入口片段验证；其余上游application/audio/protocol实现未在此环境链接。新增GUI声明/驱动API错配等已在实际编译中修正，不把格式检查代替编译。

宿主MCP最初因开发头文件缺失而跳过；后来补入匹配官方头及系统库后重跑。一次本地链接参数错误已修正，最终日志为准。release测试的example.com/Published字样是mock输出，没有发布真实Release。

## 可复现命令

完整固件（需要正常访问Component Registry）：

```sh
source /path/to/esp-idf/export.sh
idf.py --version
python3 scripts/build.py m5stack/stackchan-k151 --name m5stack-stackchan-k151 \
  --language zh-CN --wake-word nihaoxiaozhi
python3 -m unittest discover -s scripts/tests -v
```

宿主MCP测试需要C++23、cJSON和mbedTLS开发头/库；本环境使用Git的cJSON1.7.19/mbedTLS3.6.6头与系统cJSON1.7.18/mbedcrypto3.6.6动态库（ABI兼容的已用接口）：

```sh
MCP_CJSON_CFLAGS='-I/workspace/cjson-reference -I/workspace/mbedtls-host-reference/include' \
MCP_CJSON_LIBS='-L/workspace/k151-host-libs -lcjson -lmbedcrypto' \
python3 -m unittest discover -s scripts/tests -v
```

普通开发机器可使用已安装的libcjson-dev/libmbedtls-dev，省略环境变量。

独立目标编译（需要激活IDF6.1，所有Git参考由锁文件固定；已有checkout不自动重置）：

```sh
python3 scripts/check_k151_native.py --reference-root /tmp/k151-refs --work-dir /tmp/k151-check --fetch
python3 scripts/check_k151_native.py --reference-root /tmp/k151-refs --work-dir /tmp/k151-check --allow-ota
```

此脚本只产生静态库；不会生成或刷写小智固件。锁文件与IDF子模块SHA分别见native-reference-lock.json和evidence/idf-submodules.txt。

## 明确的情绪/行为映射

| 小智情绪key | 原厂基础情绪 / 装饰 |
|---|---|
| neutral、cool、confident、relaxed、winking、kissy、link、未知key | Neutral，无装饰（明确回退，不声称原厂有所有对应造型） |
| happy、laughing、funny、delicious | Happy |
| loving | Happy + Heart |
| angry | Angry + Angry装饰 |
| sad、crying | Sad |
| sleepy | Sleepy；停止空闲动作，不把LLM情绪当作电源睡眠指令 |
| doubtful（原厂）、thinking、confused | Doubt；后两者是新增情绪别名，没有新增Thinking设备状态 |
| shocked、surprised、scared、embarrassed | Doubt + Sweat |
| shy | Happy + Shy |
| silly | Happy + Dizzy |

原厂数据/几何保留，系统叠层/中文字体、20ms专用更新任务和修复后的恢复优先级属于适配。原厂非Idle任务额外等待100ms，本版取消持锁等待；视觉节奏可能不同，不能根据源码相同就保证与用户录像完全一致。

摸头反馈可跨说话状态；手松开3秒恢复最新基础情绪，丢失松手事件30秒兜底。摇晃4秒并可延长，优先于摸头；反应期间隐藏眼睛/眩晕装饰、暂停说话嘴部及空闲动作，结束/取消恢复最新状态。系统激活/升级/错误状态取消临时反馈；睡眠不覆盖通知叠层。没有独立充电等每一项专用动画，也没有音素同步或等待回答状态动画。

## 大小和资源预算

| 区域 | 分配 / 验证情况 |
|---|---|
| Flash | 16MiB；K151专用表，不沿用CoreS3默认布局 |
| NVS | 0x9000，0x4000（16KiB）；原厂校准地址保持；4个IR槽的payload约2064字节，另有NVS条目开销 |
| ota_0 | 0x20000，0x4f0000（5,177,344字节 / 4.9375MiB） |
| ota_1 | 0x510000，0x4f0000；与ota_0相同 |
| assets | 0xA00000，4MiB；新版按语言/唤醒词生成，不复制原厂App/SFX资源 |
| coredump | 0xE00000，64KiB；表内保留空间，不声称已启用完整dump收集 |
| 未分配Flash | 0xE10000到0x1000000（1.9375MiB） |
| PSRAM | 8MiB Quad；包含新版LVGL池/图像缓存、SR模型、摄像头/音频缓冲，峰值未测 |
| 新任务栈 | Avatar8KiB + sensors6KiB + IR4KiB；源配置合计18KiB，不是测得峰值 |
| IR | RX128个RMT符号/最多256时长；控制队列4帧，300ms总时长限制；空接收/NEC重复码保留上次完整帧，溢出/坏帧拒绝学习 |
| 固件.bin/ELF及assets.bin | **未知**，完整构建未进入链接/资源生成；不能报告伪造大小 |
| 原生检查库 | native-object-sizes.txt记录未链接对象text/data/bss；含删除候选段，编译优化/配置与最终固件不同，不等于Flash用量 |

许可证：板内导入文件已保留MIT/BSD声明，详见THIRD_PARTY.md。完整解析后所有传递依赖和字体/blob发行声明仍需核实。

## 未完成项

1. 在允许访问官方registry的环境完成canonical构建及代表性共享Board/OTA路径构建；检查ELF/bin、assets实际大小、链接符号和最终sdkconfig/partition。
2. 实机核验官方后台对独立K151板型的激活/协议下发、标准激活码显示/播报和MCP调用；不需要设备内模型API key，但服务可用性未由本次编译证明。
3. ST25R3916 NFC、LTR553环境光/接近、BMM150磁力计、microSD存储应用未迁移；公开工程没有对应全部行为，未宣称已支持9轴姿态。
4. 完整原厂IR学习App源码未确认；基础NEC/raw与存储已实现，长空调协议/协议状态、自动载波识别、更多学习管理UI属于后续。
5. PCF/BM8563保留初始化/读取，未自动写回官方服务器时区偏移后的系统时间；需要RTC有效性/UTC策略验收。
6. 签名可信OTA、硬件视觉对齐和全部故障边界未验证；独立project_name只能防止普通镜像误覆盖，不能替代签名。
7. HA/网易云/Pi本地接口尚未接入，没有账号令牌或外部服务部署。

## 实机验收清单

- [ ] 先只读备份并检查原厂分区/NVS，确认zero_pos_1/2在迁移前后相同；无默认全片擦除。异常NVS只进入恢复提示，不自动清空。
- [ ] 上电读取16MiB/8MiB Quad，确认PY32/SCS/Si12T/BMI270/RTC/INA226初始化与可选失败降级；GPIO0仅音频MCLK；IR5/10、SCS6/7不冲突。
- [ ] 分别测试ILI9342C/E批次版本读数、E初始化、色序/旋转/触摸坐标；核对对应批次图纸。禁止用静态编译结果当作显示验证。
- [ ] 对照用户机器录像/原厂固件验证六种情绪、五种装饰、呼吸/眨眼/眼神/头部动作与泡泡。当前只能证明25个Avatar源文件一致。
- [ ] 说话期间前/后摸头、保持触摸/松手、连续摇晃及重叠；期间服务端切换情绪、播放耗尽、打断/再次播放，确认嘴/眼/修饰器/锁均恢复。
- [ ] STT/LLM/TTS、麦克风削波/参考通道、扬声器音量/回声、唤醒词及后台MCP；嘴部随机动画不作音素同步判定。
- [ ] WiFi首次配置、断网/重连、激活码显示播报、错误通知、摄像头照片预览期间系统提示仍可读。
- [ ] 300秒视觉睡眠、头触/LCD/语音唤醒、归位完成或5秒截止后VM关断、恢复供电200ms准备时间；本版不是MCU深睡，麦克风/唤醒能力仍取决于上游功耗策略。
- [ ] 检查舵机5..85°/yaw限位、速度、归位、空闲释放、pitch堵转阈值与运行限位、任一轴反馈丢失锁止；适当负载下检查温升/噪声/电流。yaw没有电流堵转保护，PY32通信失效时不能保证软件断电。
- [ ] 自动关机路径VM先关断，不依赖控制队列容量；外接USB/电池状态切换、充满、低电量与物理关机均安全。
- [ ] IR逻辑分析仪确认38kHz/时长/极性，NEC收发、松开/长按重复、raw学习4槽保存/重启/重放、坏帧/满NVS拒绝，发射对接收回环影响。
- [ ] 默认OTA请求不打断对话；显式开启后普通CoreS3镜像在写分区前拒绝；兼容K151镜像、断电/rollback需专门测试，本任务不执行升级。
- [ ] 测量长时间运行PSRAM/内部RAM最低值、栈余量、音频丢帧/响应延迟、USB/SD方向协调、资源下载边界及完整依赖许可证。
