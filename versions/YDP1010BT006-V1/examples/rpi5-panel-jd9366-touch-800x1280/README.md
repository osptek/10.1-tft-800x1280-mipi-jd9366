# Raspberry Pi 5 · JD9366 显示 + 触摸（800×1280）

本目录为 **YDP1010BT006-V1** 在 Raspberry Pi 5 上的 **显示 + 触摸** 内核模块与 DT overlay 示例。

本目录文件：

| 文件 | 说明 |
| ---- | ---- |
| `panel-jd9366-800x1280.c` | JD9366 DRM panel 驱动 |
| `jd9366_touch.c` | 触摸驱动 |
| `Makefile` | 内核模块编译 |
| `vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay.dts` | DSI + 触摸 overlay |

若还需跑 LVGL，见同级目录 [`../rpi5-lvgl-jd9366-touch-800x1280/`](../rpi5-lvgl-jd9366-touch-800x1280/)。

---

# 1. 准备工作

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) device-tree-compiler
```

将本目录拷到树莓派后进入该目录。

# 2. 编译内核模块

```bash
make clean
make
```

# 3. 安装模块

```bash
sudo mkdir -p /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/panel/
sudo cp panel-jd9366-800x1280.ko /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/panel/

sudo mkdir -p /lib/modules/$(uname -r)/kernel/drivers/input/touchscreen/
sudo cp jd9366_touch.ko /lib/modules/$(uname -r)/kernel/drivers/input/touchscreen/

sudo depmod -a
```

开机自动加载：编辑 `/etc/modules`，末尾加上：

```text
panel-jd9366-800x1280
jd9366_touch
```

# 4. 编译并安装 Overlay

```bash
dtc -@ -I dts -O dtb -o vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay.dtbo vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay.dts
sudo cp vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay.dtbo /boot/firmware/overlays/
```

> Overlay 必须使用 `dtc -@`，否则符号修复可能失败，DTO 无法正确加载。

# 5. 启用

编辑 `/boot/firmware/config.txt`：

```bash
sudo nano /boot/firmware/config.txt
```

```text
# 关闭自动检测，避免和手动 overlay 冲突
display_auto_detect=0

dtoverlay=vc4-kms-v3d

# 启用 JD9366 显示与触摸 Overlay
dtoverlay=vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay

# 忽略官方 LCD
ignore_lcd=1
```

重启：

```bash
sudo reboot
```
