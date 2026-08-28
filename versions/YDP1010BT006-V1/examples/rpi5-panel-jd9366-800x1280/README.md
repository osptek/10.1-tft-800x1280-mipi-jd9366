# Raspberry Pi 5 · JD9366 800×1280（仅显示）

本目录为 **YDP1010BT006-V1** 在 Raspberry Pi 5 上的 **仅显示** 内核模块与 DT overlay 示例（无触摸）。

本目录文件：

| 文件 | 说明 |
| ---- | ---- |
| `panel-jd9366-800x1280.c` | JD9366 DRM panel 驱动 |
| `Makefile` | 内核模块编译 |
| `vc4-kms-dsi-jd9366-800x1280.dts` | DSI overlay |

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

# 3. 编译并安装 Overlay / 模块

```bash
dtc -@ -I dts -O dtb -o vc4-kms-dsi-jd9366-800x1280.dtbo vc4-kms-dsi-jd9366-800x1280.dts
sudo cp vc4-kms-dsi-jd9366-800x1280.dtbo /boot/firmware/overlays/
sudo mkdir -p /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/panel/
sudo cp panel-jd9366-800x1280.ko /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/panel/
sudo depmod -a
```

> Overlay 必须使用 `dtc -@`，否则符号修复可能失败，DTO 无法正确加载。

# 4. 启用

编辑 `/boot/firmware/config.txt`：

```bash
sudo nano /boot/firmware/config.txt
```

```text
# 关闭自动检测，避免和手动 overlay 冲突
display_auto_detect=0

dtoverlay=vc4-kms-v3d

dtoverlay=vc4-kms-dsi-jd9366-800x1280

# 忽略官方 LCD
ignore_lcd=1
```

重启：

```bash
sudo reboot
```
