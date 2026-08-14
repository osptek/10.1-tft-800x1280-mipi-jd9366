# 电子相册


本示例展示了在 **ESP32-P4-Function-EV-Board** 上运行的 **电子相册** 应用，提供自动幻灯片播放、视频播放、触摸手势交互、无线（HTTP）文件上传以及 USB 大容量存储访问等完整功能，全部画面经 MIPI-DSI 输出到 7-inch 1024 × 600 RGB LCD。

---

## 主要特性

* JPEG / PNG 图片解码，最高支持 1920 × 1080
* MP4（MJPEG + AAC）硬件加速播放，音画同步
* 可配置间隔的自动幻灯片播放
* 触摸手势：左右滑动切换，上下滑动调音量，单击播放/暂停，长按打开设置
* 内置 HTTP 上传网页（拖拽上传），上传后立即可播放
* USB MSC 设备模式：插入 PC 即可把 SD 卡当作 U 盘读写（自动暂停幻灯片）
* 实时显示 SD 卡容量与占用

---

## 快速开始

### 硬件准备

* **ESP32-P4-Function-EV-Board** 开发板
* 7-inch 1024 × 600 LCD（EK79007 驱动）+ 32-pin FPC 适配板
* micro-SD 卡（Class 10，≥8 GB，FAT32）
* USB-C 线缆（供电 / 下载 / USB-MSC）

1. 按下表将 LCD 适配板背面的排针与开发板对应引脚相连：

    | Screen Adapter Board | ESP32-P4-Function-EV-Board |
    | -------------------- | -------------------------- |
    | 5V (any one)         | 5V (any one)               |
    | GND (any one)        | GND (any one)              |
    | PWM                  | GPIO26                     |
    | LCD_RST              | GPIO27                     |

2. 将 32-pin FPC 排线插入开发板上的 **MIPI_DSI** 连接器。
3. 使用 USB-C 线连接 **USB-UART** 口到 PC（供电、下载及串口输出）。
4. 打开开发板侧边的 **Power** 电源开关。

### ESP-IDF 版本

* 需 **ESP-IDF release v5.5** 及以上。

请按照 [ESP-IDF 快速入门](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/get-started/index.html) 进行环境搭建。

### 配置

执行 `idf.py menuconfig`，在菜单中按以下路径进行配置：
```
Digital Photo Album Configuration  --->
    WiFi File Server Configuration  --->
        [*] 启用 WiFi 文件服务器 (WIFI_FILE_SERVER_ENABLED)
        (PhotoAlbum_WiFi) WiFi AP SSID
        (12345678)        WiFi AP 密码
        (4)               最大客户端数量
    Audio Decoder Configuration  --->
        [ ] 启用 FLAC 解码器
        [ ] 启用 OPUS 解码器
        [ ] 启用 VORBIS 解码器
        [ ] 启用 ADPCM 解码器
    Video Synchronization Configuration  --->
        [*] 启用音视频同步
```

---

## 编译与烧录

```bash
idf.py -p <PORT> build flash monitor
```

按 **Ctrl+]** 退出监视器。

启动后若 SD 卡中已有媒体文件，首张图片会立即显示；若为空则提示 *No Media*。

---

## 功能使用

### 1. SD 卡幻灯片
将图片/视频拷贝到 SD 卡 `/photos` 目录，支持格式：
* 图片：`.jpg` `.jpeg` `.png`
* 视频：`.mp4`（MJPEG + AAC，≤1080p）

插卡并复位，系统会递归扫描文件并自动开始播放。

在 SD 卡 `/wifi_config` 目录中的 `wifi_config.txt` 中填写 SSID 和密码，即可上电自动配网并获取时间实时显示在屏幕上。

### 2. HTTP 上传
Wi-Fi 连接至与 PC 同一网络，串口查看板子 IP，浏览器访问：
```
http://<板子_IP>/upload
```
拖拽文件上传，进度实时显示，上传完成即加入幻灯片。

### 3. USB 大容量存储
通过 USB-C 连接电脑后，SD 卡会被识别为可移动磁盘。  
访问期间幻灯片自动暂停，安全弹出后自动恢复。

### 4. 触摸手势
* **左 / 右滑** — 上/下一张
* **上 / 下滑** — 音量 + / –（仅视频）
* **单击** — 播放 / 暂停（视频）
* **长按** — 打开设置面板（调整幻灯片间隔）

### 注意事项

1. 通过 USB 上传或直接存储到 SD 卡的 **照片/视频分辨率** 请勿超过 **1080p**。此外，JPEG 图片的宽度与高度需保证 **8 像素对齐**，否则硬件加速解码可能失败。
2. 上传的 **MP4 / AVI** 视频需采用 **MJPEG 编码**，不支持 H.264/H.265 等其他压缩格式。