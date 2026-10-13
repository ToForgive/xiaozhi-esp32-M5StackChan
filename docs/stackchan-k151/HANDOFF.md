# K151 本机执行交接

用户最新指令：先提交并推送已完成代码，再转到本机继续。本次交接停止 Cloud 功能开发；不创建 PR、不运行 GitHub Actions、不合并、不发布、不刷机。交接分支为 `feat/stackchan-k151-idf6`，目标仓库为 https://github.com/ToForgive/xiaozhi-esp32-M5StackChan 。推送后的最终 SHA 以交接消息和 `git rev-parse HEAD` 为准。

**这是源码检查点，不是完整固件交付。尚未生成/链接可刷写 K151 bin，未进行实机安全或视觉验收。** 旧报告和旧 Library 包对应前一提交 `9754d2cb6e13616636d19dcaa13d6911c6a2894f`，本文件及 `evidence/handoff-*` 记录交接检查点的新状态。

## 已完成范围

- 保留 fork/upstream 基线 `e076ac496e2d93dc3a2308a0491d745afe9489ab`，没有退回 v2.5.2 tag 或合并主分支；完整审查原厂工程、v2.2.4补丁、HAL/board/Avatar/行为/依赖/分区/资源/启动流程，记录实际来源 SHA。
- 独立 K151 board type/name `m5stack-stackchan-k151`、app project name `xiaozhi-k151`；16MiB Flash/8MiB Quad PSRAM配置、专用原厂兼容分区、标准小智激活/官方后台、默认禁用固件OTA和兼容项目名校验。不带手机App绑定、World/AppCenter/Dance/ESP-NOW、模型API key或Pi服务。
- 保留25个原厂Avatar文件，六种基础情绪、五种装饰、呼吸/眨眼/空闲眼神与头部动作、随机说话嘴部；新增小智情绪映射、摸头与摇晃事件优先级、恢复最新状态及睡眠恢复。没有音素同步或独立Thinking设备状态。
- LCD ILI9342C/E检测、FT6336触摸、ES7210/AW88298音频、新版EspVideo/GC0308接线、AXP电池/背光、PY32供电/RGB、SCS限位/速度/校准/归位、Si12T、BMI270、RTC和INA226读取。保留新版音频机制，未复制旧音频管线。
- 原生IDF RMT NEC/raw红外收发、4个有界NVS学习槽、捕获/保存/重放MCP接口；未获得或恢复完整原厂红外App。

## 此次已修复的复核缺陷

1. **照片预览空对象风险**：不再委托默认聊天UI。SetPreviewImage只提交有界的最新图像所有权；显示worker在成功获取10ms显示锁后使用K151自己创建的preview对象，保留图片直到隐藏/替换，10秒自动隐藏。没有使用缺失的emoji_box_/content_；系统标签位于预览之上。
2. **主题锁失败和主线程长等待**：SetTheme只提交字体快照，不等待LVGL锁、不在调用者中修改LVGL。worker保留旧字体所有权直到所有样式重绑定；显示锁失败时保留待处理图片/字体供后续帧重试。
3. **睡眠跨任务访问**：K151不再使用公共PowerSaveTimer的跨任务可变字段。新PowerPolicy由hardware task单独Poll；其他任务只提交原子的WakeUp/SetEnabled。进入/退出睡眠、自动关机回调均由该owner执行。
4. **RTC/电源MCP同步I2C**：RTC、INA226和AXP快照由hardware task更新；MCP/状态显示只读缓存，5秒过期报告不可用。K151 RTC/PY32事务改为40ms超时；RTC供电失效标志使日历无效，不覆盖系统时钟。
5. **VM错误状态**：PY32新增可报告读/写错误的tryDigitalWrite，读失败不执行基于伪值的位写入。VM命令失败禁止继续运动，并明确报告physical supply unknown；关闭命令ACK不等于确认物理电源已关。关机不排队，锁止后worker不再继续运动；PMIC关闭失败输出错误/通知。没有新增yaw堵转保护。

上述修复已经通过宿主回归及实际ESP32-S3目标编译。并未运行真实LVGL画面、ESP任务竞争压力、真实电流/堵转或物理断电测试。

## 快速验证的实际结果

- `evidence/handoff-host-tests.log`：**124项全部通过，无跳过，16.918秒**。整个仓库套件；K151专属为原有5项 + 新增runtime3项，共8项，不是124项硬件测试。
- 新增runtime3项编译实际显示入口/应用方法并以宿主对象断言不调用默认UI、不等待LVGL、图像/字体所有权；运行真实PowerPolicy并发原子请求/期限测试；运行真实缓存访问方法的过期/并发测试。它们以平台对象替代LVGL/硬件，不验证真实GUI任务。
- 原有K1515项涵盖Model状态恢复、65536个NEC组合、实际OTA入口片段与分片头语句、分区与canonical选板。其余116项是原有构建/资源/CI/MCP/Ogg/发布策略等宿主测试，发布输出是mock。
- `evidence/handoff-native.log`：真实ESP-IDF v6.1/GCC15.2的ESP32-S3检查，快速增量编译15个受影响对象并重建`libmain.a`。此次为默认禁用OTA配置；旧9754提交的两种OTA配置日志仍保留。它不是整机ELF/bin链接。
- 宿主g++：Debian14.2.0；Python3.12.14；ESP-IDF v6.1提交`fff9895c82d744c7237be8847347bdd1b07c6643`。SDK官方tag为v6.1；具体Git参考见source-shas.tsv和native-reference-lock.json。
- 分离检查使用固定官方Git头/源；部分验证参考esp_audio_codec2.6.2/esp_audio_effects1.4.3更新于main manifest要求的~2.5/~1.3。这只能证明该参考组合的API编译，不能替代canonical按manifest解析后的编译/链接/ABI验证。没有捏造registry元数据。

## 完整构建的准确阻塞

完整canonical构建在CMake依赖解析阶段失败，未进入整机源文件编译/链接：

```text
ERROR: Cannot establish a connection to the component registry. Are you connected to the internet?
URL: https://components-file.espressif.com/components/78/esp-ml307.json
cmake failed with exit code 1
```

只读curl探测为`curl: (56) CONNECT tunnel failed, response 403`，头为HTTP403、server: envoy。Cloud环境HTTPS_PROXY/HTTP_PROXY指向`http://proxy:8080`，没有用户凭据。这是代理在CONNECT阶段拒绝，不是官方服务的应用层403；证据见`evidence/handoff-registry-probe.txt`。浏览工具同样不能访问该请求。

已尝试：canonical构建及一次workspace cache路径修正后的重跑；只读网络探测（含先前沙盒外批准的探测），均没有解除registry拒绝；GitHub公开源码正常可读，已做有限的官方Git参考检查，尚未形成全依赖Git/local profile。未关闭TLS/下载包校验、未禁用代理绕过策略、未使用未知镜像或伪造组件记录。此前dl.espressif.com的Python constraints下载403已用官方无constraints安装选项解决，不是当前完整构建失败点。

最小解除：在本机正常访问官方Component Registry及其下载地址，按下面canonical命令构建。无需模型API key或新增账号令牌；若本机也拒绝，则通过本机正常网络策略允许官方服务访问，逐条记录随后出现的实际失败URL。官方组件管理器支持Git/local源码依赖，但该替代配置尚未完成，不可称已有可复现完整Git构建。

## 本机继续步骤

1. 只在本机继续该分支，先确认没有本机未提交工作；Cloud交接推送后停止写代码，避免并发。

```sh
git clone --branch feat/stackchan-k151-idf6 --single-branch \
  https://github.com/ToForgive/xiaozhi-esp32-M5StackChan.git
cd xiaozhi-esp32-M5StackChan
git status --short
git log -1 --oneline
```

2. 激活官方ESP-IDF6.1；已有环境不重复安装。没有SDK时可用官方v6.1源码及安装器：

```sh
git clone --branch v6.1 --depth 1 --recursive https://github.com/espressif/esp-idf.git ../esp-idf
../esp-idf/install.sh esp32s3
source ../esp-idf/export.sh
idf.py --version
```

3. 完整构建，记录首个失败点或完整成功证据：

```sh
python3 scripts/build.py m5stack/stackchan-k151 --name m5stack-stackchan-k151 \
  --language zh-CN --wake-word nihaoxiaozhi
```

需拿到真实`xiaozhi-k151.elf/.bin`、assets.bin、bootloader/partition-table、merged-binary.bin及flasher_args.json。检查`idf.py size`/`size-components`、最终sdkconfig、bin/app_desc的独立项目名、Flash分区适配和PSRAM配置；不能用Cloud archive大小代替固件大小。`merged-binary.bin`不是默认安全刷写入口，必须先完成以下校准/分区备份流程。

4. 宿主回归需要C++23、cJSON和mbedTLS开发包。Ubuntu可安装g++、libcjson-dev/libmbedtls-dev后执行：

```sh
python3 -m unittest discover -s scripts/tests -v
```

Cloud曾使用MCP_CJSON_CFLAGS/MCP_CJSON_LIBS指向本地官方头和系统库，仅为解决Cloud开发包缺失；本机优先使用正式开发包。需要分离目标检查时，脚本会获取native-reference-lock.json的真实Git来源，不改已有checkout：

```sh
python3 scripts/check_k151_native.py --reference-root /tmp/k151-refs --work-dir /tmp/k151-native --fetch
python3 scripts/check_k151_native.py --reference-root /tmp/k151-refs --work-dir /tmp/k151-native --allow-ota
```

5. 继续未完成外设和实机验收，而不是把软件缺失写成“仅缺设备测试”。完整清单在下文及validation.md。

## 未完成的实现与待验收

| 项目 | 交接时状态 | 本机工作 |
|---|---|---|
| NFC ST25R3916 | **未集成** | 本轮发现最新版官方M5Unit-NFC已提供pure-IDF CMake/manifest和现有i2c_master_bus_handle_t适配，地址0x50；旧审查“需要独立Arduino移植”的判断已过时。参考SHA已记录，但没有把库/依赖加入主工程或完成reader任务/MCP。应固定M5Unit-NFC/M5UnitUnified/M5HAL/M5Utility SHA，先做只读标签接口，使用独立owner任务/缓存，勿恢复账号App |
| LTR553 | **未实现** | 核对CoreS3的公开寄存器/官方驱动与地址，实现ALS/接近读取、缓存与工具；没有环境光动画承诺 |
| BMM150 | **未实现** | 已拉取Bosch SensorAPI和M5Unified参考；M5公开实现地址0x10，软复位/30Hz配置可参考；需要补偿/轴向和与BMI270共总线验证，不能宣称9轴融合已完成 |
| microSD | **未实现** | CoreS3 SD与LCD共享SPI，GPIO35兼作SD MISO和LCD D/C；需事务/方向协调和真实分区/卡挂载测试，不能直接套用普通SDSPI初始化 |
| IR | 基础NEC/raw、四槽学习/重放已实现 | 自动化NEC逻辑测试已做；真实RMT/收发波形、NVS满/坏blob/掉电/重启、更多协议与工具异步状态仍需强化；不是完整原厂App |
| RTC | 缓存只读、供电失效/日历检查已实现 | 不自动修改官方后台时区语义；校时/UTC策略及实机有效性仍待完成 |
| 显示/行为/音频 | 软件接入与上述修复已做 | 实机比较六情绪/五装饰，尤其说话中摸头/摇晃、打断、断网、睡眠唤醒；随机嘴部不是音素同步。没有现用固件/录像，不能保证用户机视觉一致 |
| 安全、激活、资源 | 部分代码策略已做 | 完整bin/预算、官方后台独立板型激活/MCP、长期RAM/栈/总线时延、签名OTA和完整传递依赖/字体/blob许可清单未验证 |

## 校准、分区与回滚边界

- 既有`servo/zero_pos_1`、`zero_pos_2`保留；无效或缺失只在内存回退460/620。显式校准方法才写NVS，未向对话MCP开放。K151 NVS异常不自动erase；目前实际是日志后abort/reboot，没有屏幕恢复向导。
- NVS0x9000/0x4000，ota_0=0x20000/0x4f0000，ota_1=0x510000/0x4f0000，assets=0xA00000/4MiB，coredump=0xE00000/64KiB，分区尾0xE10000，小于16MiB。固件/资源真实大小与PSRAM峰值未知。
- 不能自动把新分区表应用到旧设备。首次刷写前需只读备份**全16MiB Flash**、原始partition-table和NVS，确认两轴校准；在机器上留存可校验SHA256的原厂完整备份/恢复指令。不得默认erase-flash；不得未经核对直接写merged镜像。
- 只有完整构建和原始布局核对后才能写app/资源等确定分区。默认OTA关闭；rollback配置已启用但未做电断/失败启动测试，不能保证旧原厂镜像与新otadata流程兼容。
- 可在正式进入刷机阶段参考官方esptool的`read-flash 0 0x1000000 factory-backup.bin`备份、`read-flash 0x9000 0x4000 nvs-backup.bin`再单独确认原表；恢复备份会恢复原应用与NVS，操作前必须确认芯片/Flash容量/端口/备份校验，不能把这些说明当作本次已执行。详细可执行刷写/回滚流程须在拿到真实产物和设备布局后完善。
- yaw±128°、pitch5..85°；仅pitch有反馈堵转检测/收紧限位。任一轴三次反馈失败锁止该轴到重启并尝试TorqueOff，**不自动切断VM**；UART失联时无法保证命令送达。供电未知时运动抑制，正常非shutdown状态可显式请求servo_power重新尝试，成功ACK也不是物理确认；关机锁止需要重启恢复，均不清校准。

## GitHub Actions与Cloud专有资料

现有`.github/workflows/build.yml`使用`espressif/idf:v6.1`和canonical脚本，K151选板链已经加入。其push分支过滤只含main/ci/*，手动dispatch会跑整个矩阵；现有test-mcp另有path过滤。没有新增专用K151 workflow，也没有触发任何CI。以后若用户授权CI，最小改动是增加只允许workflow_dispatch的K151单板任务并保存ELF/bin/assets/分区/size日志；不要使用Release workflow代替验证。

以下只在Cloud，**不随提交包含**：`/workspace/esp-idf`及工具链/Python环境、所有`*-reference`源码checkout、`k151-native-verified`build/static archive、临时helper/Library本地副本和构建缓存。用户可用source-shas.tsv中官方URL/SHA重新取得源码；scripts/check_k151_native.py及其锁文件随代码提交。必要host/目标检查/registry拒绝日志已复制到仓库，未加入依赖仓库或大体积构建产物、凭据和secrets。

旧Library交付保存的是前一检查点，不能视为当前提交的完整固件。推送后的GitHub分支/提交是本机接续的源，Cloud交接后不继续改该分支。
