# 10.1" 800×1280 TFT MIPI module (JD9366) — documentation & samples

**简体中文：** [`README.md`](README.md)

---

> This repository provides **sample projects** for this module, together with datasheets, specifications, and interface / bring-up documentation for selection reference and integration.

## Product overview

| Item | Description |
|:--|:--|
| Module | 10.1-inch **TFT** panel, **800×1280** resolution |
| Interface | **MIPI** |
| Driver IC | **JD9366** |
| Spec ID | **`10.1-tft-800x1280-mipi-jd9366`** is the common product designation in documentation |

---

## Repository layout

### Top-level

| Path | Contents |
|:--|:--|
| `docs/` | Datasheets, specifications, adapter schematics |
| `examples/` | **Sample projects** grouped by feature |

### `examples/` layout

| Location | Description (internal package folder) |
|:--|:--|
| `examples/` root | **esp-idf代码** (esp-lvgl-port + LVGL9) |
| `mjpeg/` | MJPEG samples (**mjpeg代码**) |
| `esp-album/` | Fish-eagle album demo (**esp-album-yuying-10.1**) |
| `yuying-ppa/` | Fish-eagle PPA display demo (**yuying_10_lcd_jd9366_ppa**) |

### Sample project paths

#### Baseline (`examples/` root)

| Description | Path |
|:--|:--|
| esp-lvgl-port + LVGL9 | `examples/esp32p4-idf5_jd9366-mipi_esp-lvgl-port_lvgl9/` |

#### MJPEG (`mjpeg/`)

| Description | Path |
|:--|:--|
| MJPEG decode | `examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode/` |
| MJPEG decode + LVGL9 | `examples/mjpeg/p4-idf_jd9366-mipi_mjpeg-decode_lvgl-v9/` |

#### Album (`esp-album/`)

| Description | Path |
|:--|:--|
| esp-album-yuying-10.1 | `examples/esp-album/` |

#### PPA (`yuying-ppa/`)

| Description | Path |
|:--|:--|
| yuying_10_lcd_jd9366_ppa | `examples/yuying-ppa/` |
