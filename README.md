# ESP32-S3 3.5寸触摸屏综合工程

本项目基于 [ESP32-S3-LVGL-Board](https://github.com/ConstStrings/ESP32-S3-LVGL-Board) 二次开发，
适配 ESP32-S3 N16R8 3.5 寸电容触摸屏开发套件。

工程使用 ESP-IDF + LVGL，当前工程名为 `synthesis`，主要功能包括主界面、相册、视频播放、WiFi、日历和网络时间同步等。

## 来源说明

- 上游开源项目：[ConstStrings/ESP32-S3-LVGL-Board](https://github.com/ConstStrings/ESP32-S3-LVGL-Board)
- 适配硬件：慧勤智远 ESP32-S3 N16R8 3.5 寸电容触摸屏开发套件
- 商品页面：[淘宝商品链接](https://item.taobao.com/item.htm?id=946264202563)

## 硬件
 
- 主控：ESP32-S3 N16R8
- 屏幕：3.5 寸 480x320 电容触摸屏
- 扩展 IO：XL9555
- 存储：SD 卡，SPI 驱动
- 开发框架：ESP-IDF 5.4.1

## 工程目录

```text
synthesis/
├── components/          # BSP、LVGL、中间件和驱动组件
├── main/                # 应用代码
├── managed_components/  # ESP-IDF 组件管理器生成目录
├── build/               # 编译生成目录
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

当前视频播放做过长期播放优化：

- 移除了视频路径里没有实际使用的帧定时器，避免长时间循环播放后产生无意义周期回调。
- 同分辨率视频会复用解码输出缓冲，减少反复申请/释放大块内存导致的碎片风险。

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
