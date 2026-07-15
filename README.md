## 免编译烧录

仓库的 `build/idf6/` 目录包含已构建好的 ESP32-S3 固件，无需安装 ESP-IDF 或重新编译即可烧录。Git 会直接跟踪下面 3 个文件，不需要复制到其他目录：

| 文件 | 烧录地址 |
| --- | ---: |
| `build/idf6/bootloader/bootloader.bin` | `0x0` |
| `build/idf6/partition_table/partition-table.bin` | `0x8000` |
| `build/idf6/synthesis.bin` | `0x10000` |

使用 esptool 时，在工程根目录执行：

```powershell
esptool --chip esp32s3 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 build/idf6/bootloader/bootloader.bin 0x8000 build/idf6/partition_table/partition-table.bin 0x10000 build/idf6/synthesis.bin
```

烧录前请关闭占用串口的程序，并选择正确的 ESP32-S3 串口；烧录完成后设备会自动复位。

# ESP32-S3 3.5寸触摸屏综合工程

这是一个面向 ESP32-S3 触摸屏的多功能桌面工程，集成应用启动器、图片与视频播放、计算器、日历、WiFi 时间同步，以及通过蓝牙显示 Windows 端 Codex 和 Claude 运行状态的 AI 状态面板。


工程使用 ESP-IDF + LVGL，当前工程名为 `synthesis`，主要功能包括主界面、相册、视频播放、计算器、日历、WiFi、网络时间同步和 AI 状态显示等。

## 来源说明

- 适配硬件：慧勤智远 ESP32-S3 N16R8 3.5 寸电容触摸屏开发套件
- 商品页面：[淘宝商品链接](https://item.taobao.com/item.htm?id=946264202563)

## 硬件
 
- 主控：ESP32-S3 N16R8
- 屏幕：3.5 寸 480x320 电容触摸屏
- 扩展 IO：XL9555
- 存储：SD 卡，SPI 驱动
- 开发框架：ESP-IDF 6.0.1

## 工程目录

```text
synthesis/
├── components/          # BSP、LVGL、中间件和驱动组件
├── main/                # 应用代码
├── managed_components/  # ESP-IDF 组件管理器生成目录
├── build/               # 编译生成目录（Git 仅跟踪 3 个烧录文件）
├── tools/               # PC 端 AI 状态 BLE 桥接程序
├── sdkconfig            # 当前工程配置
├── sdkconfig.defaults   # 关键硬件配置默认值
└── partitions-16MiB.csv # 分区表
```

## 工程配置

本工程当前目标芯片为 `esp32s3`，Flash 为 16MB，PSRAM 为 Octal 80MHz，CPU 主频为 240MHz，并使用 `partitions-16MiB.csv` 自定义分区表。

## SD 卡目录

SD 卡根目录需要按下面方式放文件：

```text
SD:/
├── VIDEO/    # 视频文件
└── PICTURE/  # 图片文件
```

目录名建议全部大写，和代码里的路径保持一致。

## AI 状态应用

“AI 状态”应用通过 Bluetooth LE 接收 Windows 电脑上的 Codex 和 Claude 状态，不依赖 WiFi。ESP32 负责配对、保存绑定关系和显示状态，PC 端的 `PcAiBleBridge.exe` 负责读取本机状态并发送给屏幕。

界面显示内容：

- Codex 和 Claude 的总体状态：`OFF`、`IDLE`、`BUSY` 或 `UNKNOWN`。。
- Codex 任务状态：`OFF`、`IDLE`、`BUSY`、`WAIT`、`DONE`、`FAILED` 或 `STALE`。
- `DONE` 任务保留约 5 分钟，`FAILED` 任务保留约 1 分钟。
- 状态使用不同颜色区分：空闲为绿色、忙碌为蓝色、等待为黄色、完成为青绿色、失败为红色，关闭和过期状态为灰色。

### 构建 PC 桥接程序

桥接工具位于 `tools/pc_ai_ble_bridge/`。Windows PowerShell 的脚本执行策略可能阻止 `build.ps1`，因此建议直接运行不受该策略影响的 `build.cmd`：

```powershell
Set-Location .\tools\pc_ai_ble_bridge
.\build.cmd --overwrite
```

构建成功后会生成：

```text
tools/pc_ai_ble_bridge/bin/PcAiBleBridge.exe
```

构建脚本使用 Windows 自带的 .NET Framework C# 编译器和 Windows Bluetooth API，不需要修改 PowerShell 执行策略。

### 首次配对和使用

1. 烧录并启动 ESP32 工程，在主界面打开“AI 状态”应用。
2. 点击屏幕上的 `PAIR`，进入限时配对状态。
3. 打开 Windows“设置 > 蓝牙和其他设备 > 添加设备 > 蓝牙 > 显示所有设备”，选择 `ESP32-AI-Status`，按屏幕和 Windows 提示确认配对码。
4. 配对成功后，屏幕会短暂显示约 3 秒的 `BONDED`。
5. 运行：`PcAiBleBridge.exe`


桥接程序会优先查找已经在 Windows 中配对的屏幕，连接成功后约每 2 秒同步一次状态。一个用户会话只能运行一个桥接程序实例，按 `Ctrl+C` 可以停止。

以后使用时，只需打开屏幕上的“AI 状态”应用并运行 `PcAiBleBridge.exe`，不需要重复配对。

如果 Windows 显示已经配对但桥接程序始终无法发现服务，请先停止桥接程序，在 Windows 蓝牙设置中删除 `ESP32-AI-Status`，再点击屏幕上的 `FORGET`，随后重新执行上述配对步骤。

效果图:
![alt text](cfdf7d676a8d1852d4a3e25deebca865-3.jpg)


## 视频文件

视频功能读取 `SD:/VIDEO/` 目录下的视频文件。

推荐格式：

- 容器：AVI
- 编码：MJPEG
- 分辨率：480x320

其他视频格式无法播放


视频界面操作：

- 长按屏幕上半部分：上一个视频
- 长按屏幕下半部分：下一个视频
- 长按四个角任意一个热区：返回主界面
- 单个视频播放结束后：自动从头循环播放


## 相册文件

相册功能读取 `SD:/PICTURE/` 目录下的图片文件。

支持格式：

- JPG/JPEG
- PNG
- BMP

相册界面操作：

- 点击左侧箭头或左侧热区：上一张图片
- 点击右侧箭头或右侧热区：下一张图片
- 点击或长按返回键：返回主界面，具体以界面上的返回按钮为准

相册会为部分图片生成缓存，用于加快二次打开和切换速度。图片文件发生变化后，如果显示异常，可以删除对应缓存后重新进入相册。

## WiFi 和时间

- WiFi 支持扫描和连接。
- WiFi 密码会保存到 NVS，下次连接同一热点时可复用。
- 时间支持联网同步，并用于主界面和日历显示。
