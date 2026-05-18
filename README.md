# 10.1 寸 800×1280 TFT MIPI 模组（JD9366）资料与示例

**English：** [`README_EN.md`](README_EN.md)

---

> 本仓库提供该模组的 **示例工程**，以及数据手册、规格与接口说明等资料，便于选型参考与集成开发。

## 产品概要

| 项目 | 说明 |
|:--|:--|
| 模组规格 | 10.1 英寸 **TFT**，分辨率 **800×1280** |
| 接口 | **MIPI** |
| 驱动芯片 | **JD9366** |
| 规格标识 | 产品资料中常用 **`10.1-tft-800x1280-mipi-jd9366`** 表示本规格 |

---

## 仓库结构

### 顶层目录

| 路径 | 说明 |
|:--|:--|
| `docs/` | 数据手册、规格说明、转接板原理图等 |
| `examples/` | 按功能分类的 **示例工程** |

### `examples/` 分类

| 分类 | 说明（对应内部资料目录） |
|:--|:--|
| `examples/` 根目录 | **esp-idf代码**（esp-lvgl-port + LVGL9） |
| `mjpeg/` | **mjpeg代码** |
| `esp-album/` | **esp-album-yuying-10.1** |
| `yuying-ppa/` | **yuying_10_lcd_jd9366_ppa** |

### 示例工程路径

#### 基础（`examples/` 根目录）

| 说明 | 路径 |
|:--|:--|
| esp-lvgl-port + LVGL9 | `examples/esp32p4-idf5_jd9366-mipi_esp-lvgl-port_lvgl9/` |

#### mjpeg代码（`mjpeg/`）

| 说明 | 路径 |
|:--|:--|
| MJPEG 解码 | `examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode/` |
| MJPEG 解码 + LVGL9 | `examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode_lvgl-v9/` |

#### esp-album-yuying-10.1（`esp-album/`）

| 说明 | 路径 |
|:--|:--|
| 鱼鹰相册示例 | `examples/esp-album/` |

#### yuying_10_lcd_jd9366_ppa（`yuying-ppa/`）

| 说明 | 路径 |
|:--|:--|
| 鱼鹰 PPA 显示示例 | `examples/yuying-ppa/` |
