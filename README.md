<p align="left"><img alt="OSPTEK" src="./images/logo.png" width="200" /></p>

<h1 align="center">OSPTEK 10.1″ TFT 800×1280（JD9366 · MIPI）</h1>

<p align="center"><b>触摸 TFT 模组 · MIPI DSI · JD9366</b></p>

<p align="center"><a href="./README_EN.md">English</a> | 简体中文</p>

<p align="center">
  <img alt="Size: 10.1 inch" src="https://img.shields.io/badge/Size-10.1%22-3498DB?style=flat-square" />
  <img alt="Resolution: 800x1280" src="https://img.shields.io/badge/Resolution-800%C3%971280-8E44AD?style=flat-square" />
  <img alt="Interface: MIPI" src="https://img.shields.io/badge/Interface-MIPI-27AE60?style=flat-square" />
  <img alt="Driver: JD9366" src="https://img.shields.io/badge/Driver-JD9366-E7352C?style=flat-square" />
</p>

<p align="center"><img alt="OSPTEK 10.1 寸 800×1280 TFT MIPI 模组（JD9366）宣传图" src="./images/product.png" width="640" /></p>

## 目录

- [产品简介](#产品简介)
- [规格参数](#规格参数)
- [示例工程](#示例工程)
- [仓库结构](#仓库结构)
- [相关资料](#相关资料)
- [购买链接](#购买链接)
- [技术支持](#技术支持)

---

## 产品简介

OSPTEK **10.1 寸 800×1280 TFT** 是一款 **MIPI DSI** 接口彩色显示模组，显示驱动为 **JD9366**，模组带触摸接口（示例使用 `esp_lcd_touch_jd9366`）。适合中大尺寸 HMI、竖屏信息面板与多媒体显示等场景。

规格标识（仓库名）：`10.1-tft-800x1280-mipi-jd9366`

当前模组版本：**YDP1010BT006-V1**。电气与外形细节以 [`docs/YDP1010BT006-V1.pdf`](./docs/YDP1010BT006-V1.pdf) 为准。

## 规格参数

| 项目 | 规格 |
| ---- | ---- |
| 尺寸 | 10.1 英寸 |
| 类型 | TFT / IPS（彩色） |
| 分辨率 | 800×1280 |
| 接口 | MIPI DSI |
| 驱动 IC | JD9366 |
| 触摸驱动 | JD9366 |

> 完整外形尺寸、FPC 定义、供电与时序以产品规格书 / 驱动手册为准。

## 示例工程

| 说明 | 路径 |
| ---- | ---- |
| ESP32-P4 · JD9366 MIPI + esp-lvgl-port / LVGL9 | [`examples/esp32p4-idf5_jd9366-mipi_esp-lvgl-port_lvgl9/`](./examples/esp32p4-idf5_jd9366-mipi_esp-lvgl-port_lvgl9/) |
| ESP32-P4 · MJPEG 解码 | [`examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode/`](./examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode/) |
| ESP32-P4 · MJPEG 解码 + LVGL9 | [`examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode_lvgl-v9/`](./examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode_lvgl-v9/) |
| ESP32-P4 · 鱼鹰相册示例 | [`examples/esp-album/`](./examples/esp-album/) |
| ESP32-P4 · 鱼鹰 PPA 显示示例 | [`examples/yuying-ppa/`](./examples/yuying-ppa/) |

## 仓库结构

```text
10.1-tft-800x1280-mipi-jd9366/
├── README.md
├── README_EN.md
├── MODULE_VERSION.md
├── LICENSE
├── images/          # README 用图
├── docs/            # 规格书、驱动手册、转接板原理图等
└── examples/        # 示例工程
```

## 相关资料

### 本产品资料

| 资料 | 链接 |
| ---- | ---- |
| 产品规格书（YDP1010BT006-V1） | [`docs/YDP1010BT006-V1.pdf`](./docs/YDP1010BT006-V1.pdf) |
| 驱动 IC 数据手册（JD9366） | [`docs/JD9366TC_DS_V0.01_20220426.pdf`](./docs/JD9366TC_DS_V0.01_20220426.pdf) |
| 10.1 寸屏幕 P4 转接板 V1.1 | [`docs/PCB-10.1-P4-adapter-V1.1.pdf`](./docs/PCB-10.1-P4-adapter-V1.1.pdf) |
| 转接板原理图截图 | [`docs/adapter-board-schematic.jpg`](./docs/adapter-board-schematic.jpg) |
| 初始化序列（MIPI 2 lane） | [`docs/JD9366TC_CSOT10.1_MIPI2L_init.txt`](./docs/JD9366TC_CSOT10.1_MIPI2L_init.txt) |
| 初始化序列（MIPI 4 lane） | [`docs/JD9366TC_CSOT10.1_MIPI4L_init_20250320.txt`](./docs/JD9366TC_CSOT10.1_MIPI4L_init_20250320.txt) |

### 示例工程

- [ESP32-P4 JD9366 MIPI + LVGL9](./examples/esp32p4-idf5_jd9366-mipi_esp-lvgl-port_lvgl9/)
- [ESP32-P4 MJPEG 解码](./examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode/)
- [ESP32-P4 MJPEG 解码 + LVGL9](./examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode_lvgl-v9/)
- [ESP32-P4 鱼鹰相册](./examples/esp-album/)
- [ESP32-P4 鱼鹰 PPA](./examples/yuying-ppa/)

## 购买链接

<p align="center">
  <a href="https://shop110742373.taobao.com/"><img alt="淘宝官方店铺" src="https://img.shields.io/badge/淘宝-官方店铺-FF6A00?style=for-the-badge" /></a>
  &nbsp;&nbsp;
  <a href="https://www.aliexpress.com/store/1105701619"><img alt="速卖通官方店铺" src="https://img.shields.io/badge/速卖通-官方店铺-FF6A00?style=for-the-badge" /></a>
</p>

**国内（淘宝）**

- 店铺：[鱼鹰光电工厂店](https://shop110742373.taobao.com/)

**海外（AliExpress）**

- 店铺：[OSPTEK Official Store](https://www.aliexpress.com/store/1105701619)

## 技术支持

- 技术支持 / 产品咨询：<luyu@osptek.com>
- QQ 技术交流群：**985881096**
- 公司官网：<https://osptek.com/>
- 使用中如有问题，也可在本仓库 [Issues](https://github.com/osptek/10.1-tft-800x1280-mipi-jd9366/issues) 中提问

---

<p align="center"><sub>© 2026 OSPTEK 鱼鹰光电 · 本仓库资料采用 CC BY 4.0 许可</sub></p>
