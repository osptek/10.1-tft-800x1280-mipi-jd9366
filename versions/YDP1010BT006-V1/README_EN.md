<p align="left"><img alt="OSPTEK" src="./images/logo.png" width="200" /></p>

<h1 align="center">OSPTEK 10.1″ TFT 800×1280 (JD9366 · MIPI)</h1>

<p align="center"><b>Touch TFT module · MIPI DSI · JD9366</b></p>

<p align="center"><a href="./README.md">简体中文</a> | English · <a href="../../README_EN.md">Family index</a></p>

<p align="center">
  <img alt="Size: 10.1 inch" src="https://img.shields.io/badge/Size-10.1%22-3498DB?style=flat-square" />
  <img alt="Resolution: 800x1280" src="https://img.shields.io/badge/Resolution-800%C3%971280-8E44AD?style=flat-square" />
  <img alt="Interface: MIPI" src="https://img.shields.io/badge/Interface-MIPI-27AE60?style=flat-square" />
  <img alt="Driver: JD9366" src="https://img.shields.io/badge/Driver-JD9366-E7352C?style=flat-square" />
</p>

<p align="center"><img alt="OSPTEK 10.1″ 800×1280 TFT MIPI module (JD9366) product image" src="./images/product.png" width="640" /></p>

## Contents

- [Overview](#overview)
- [Specifications](#specifications)
- [Sample projects](#sample-projects)
- [Repository layout](#repository-layout)
- [Resources](#resources)
- [Buy](#buy)
- [Support](#support)

---

## Overview

OSPTEK **10.1″ 800×1280 TFT** is a **MIPI DSI** color display module driven by **JD9366**, with a touch interface (samples use `esp_lcd_touch_jd9366`). It suits mid/large HMI, portrait info panels, and multimedia displays.

Spec ID (repository name): `10.1-tft-800x1280-mipi-jd9366`

Current module version: **YDP1010BT006-V1**. Electrical and mechanical details follow [`docs/YDP1010BT006-V1.pdf`](./docs/YDP1010BT006-V1.pdf).

## Specifications

| Item | Spec |
| ---- | ---- |
| Size | 10.1 inch |
| Type | TFT / IPS (color) |
| Resolution | 800×1280 |
| Interface | MIPI DSI |
| Driver IC | JD9366 |
| Touch IC | JD9366 |

> Full outline, FPC definition, power, and timing follow the product datasheet / driver IC datasheet.

## Sample projects

| Description | Path |
| ---- | ---- |
| ESP32-P4 · JD9366 MIPI + esp-lvgl-port / LVGL9 | [`examples/esp32p4-idf5_jd9366-mipi_esp-lvgl-port_lvgl9/`](./examples/esp32p4-idf5_jd9366-mipi_esp-lvgl-port_lvgl9/) |
| ESP32-P4 · MJPEG decode | [`examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode/`](./examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode/) |
| ESP32-P4 · MJPEG decode + LVGL9 | [`examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode_lvgl-v9/`](./examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode_lvgl-v9/) |
| ESP32-P4 · OSPTEK album demo | [`examples/esp-album/`](./examples/esp-album/) |
| ESP32-P4 · OSPTEK PPA display demo | [`examples/yuying-ppa/`](./examples/yuying-ppa/) |
| Raspberry Pi 5 · JD9366 800×1280 panel / DT overlay (display only) | [`examples/rpi5-panel-jd9366-800x1280/`](./examples/rpi5-panel-jd9366-800x1280/) |
| Raspberry Pi 5 · JD9366 display + touch / DT overlay | [`examples/rpi5-panel-jd9366-touch-800x1280/`](./examples/rpi5-panel-jd9366-touch-800x1280/) |

## Repository layout

```text
10.1-tft-800x1280-mipi-jd9366/                                # repo root (nav: ../../README_EN.md)
└── versions/
    └── YDP1010BT006-V1/                                # full materials for this part number
        ├── README.md
        ├── README_EN.md
        ├── images/
        ├── docs/
        └── examples/
```

## Resources

### Product files

| Resource | Link |
| ---- | ---- |
| Product datasheet (YDP1010BT006-V1) | [`docs/YDP1010BT006-V1.pdf`](./docs/YDP1010BT006-V1.pdf) |
| Driver IC datasheet (JD9366) | [`docs/JD9366TC_DS_V0.01_20220426.pdf`](./docs/JD9366TC_DS_V0.01_20220426.pdf) |
| 10.1″ LCD P4 adapter board V1.1 | [`docs/PCB-10.1-P4-adapter-V1.1.pdf`](./docs/PCB-10.1-P4-adapter-V1.1.pdf) |
| Adapter board schematic (screenshot) | [`docs/adapter-board-schematic.jpg`](./docs/adapter-board-schematic.jpg) |
| Init sequence (MIPI 2-lane) | [`docs/JD9366TC_CSOT10.1_MIPI2L_init.txt`](./docs/JD9366TC_CSOT10.1_MIPI2L_init.txt) |
| Init sequence (MIPI 4-lane) | [`docs/JD9366TC_CSOT10.1_MIPI4L_init_20250320.txt`](./docs/JD9366TC_CSOT10.1_MIPI4L_init_20250320.txt) |

### Samples

- [ESP32-P4 JD9366 MIPI + LVGL9](./examples/esp32p4-idf5_jd9366-mipi_esp-lvgl-port_lvgl9/)
- [ESP32-P4 MJPEG decode](./examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode/)
- [ESP32-P4 MJPEG decode + LVGL9](./examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode_lvgl-v9/)
- [ESP32-P4 OSPTEK album](./examples/esp-album/)
- [ESP32-P4 OSPTEK PPA](./examples/yuying-ppa/)
- [Raspberry Pi 5 JD9366 panel (display only)](./examples/rpi5-panel-jd9366-800x1280/)
- [Raspberry Pi 5 JD9366 display + touch](./examples/rpi5-panel-jd9366-touch-800x1280/)

## Buy

<p align="center">
  <a href="https://www.aliexpress.com/store/1105701619"><img alt="AliExpress store" src="https://img.shields.io/badge/AliExpress-Official_Store-FF6A00?style=for-the-badge" /></a>
  &nbsp;&nbsp;
  <a href="https://shop110742373.taobao.com/"><img alt="Taobao store" src="https://img.shields.io/badge/Taobao-Official_Store-FF6A00?style=for-the-badge" /></a>
</p>

**Overseas (AliExpress)**

- Store: [OSPTEK Official Store](https://www.aliexpress.com/store/1105701619)

**China (Taobao)**

- Store: [鱼鹰光电工厂店](https://shop110742373.taobao.com/)

## Support

- Technical support / product inquiry: <luyu@osptek.com>
- QQ group (China): **985881096**
- Website: <https://osptek.com/>
- Feel free to open an Issue in this repository if you have any questions

---

<p align="center"><sub>© 2026 OSPTEK · Materials in this repository are licensed under CC BY 4.0</sub></p>
