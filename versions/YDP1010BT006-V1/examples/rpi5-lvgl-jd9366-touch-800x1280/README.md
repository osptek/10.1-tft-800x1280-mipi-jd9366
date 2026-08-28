# Raspberry Pi 5 · JD9366 显示 + 触摸 · LVGL（800×1280）

本目录为 **YDP1010BT006-V1** 在 Raspberry Pi 5 上跑 **LVGL（lv_port_linux / DRM + EVDEV）** 的参考：含显示/触摸内核模块、DT overlay，以及替换 `lv_port_linux` 用的 `main.c`。

本目录文件：

| 文件 | 说明 |
| ---- | ---- |
| `panel-jd9366-800x1280.c` | JD9366 DRM panel 驱动 |
| `jd9366_touch.c` | 触摸驱动 |
| `Makefile` | 内核模块编译 |
| `vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay.dts` | DSI + 触摸 overlay |
| `main.c` | 拷入 `lv_port_linux/src/main.c` 的参考实现（默认 800×1280 / DRM / EVDEV） |

仅点亮显示+触摸、不跑 LVGL 时，也可只用同级 [`../rpi5-panel-jd9366-touch-800x1280/`](../rpi5-panel-jd9366-touch-800x1280/)。

---

# 1. 准备工作

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r) device-tree-compiler
```

将本目录拷到树莓派后进入该目录。

# 2. 编译并安装内核模块

```bash
make clean
make

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

# 3. 编译并安装 Overlay

```bash
dtc -@ -I dts -O dtb -o vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay.dtbo vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay.dts
sudo cp vc4-kms-dsi-jd9366-lcd-touch-800x1280-overlay.dtbo /boot/firmware/overlays/
```

> Overlay **必须**使用 `dtc -@`，否则符号修复可能失败，DTO 无法正确加载。

# 4. 启用 Overlay

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

```bash
sudo reboot
```

# 5. 关闭桌面（若有桌面；无桌面可跳过）

## 5.1 开机进入命令行

```bash
sudo raspi-config
```

- `1 System Options` → `S5 Boot` → 选 **Console**
- `1 System Options` → `S6 Auto Login`：按需要选择是否自动登录
- 退出后重启：`sudo reboot`

## 5.2 用户组权限

```bash
sudo usermod -aG video,input,render $USER
```

重新登录或重启后生效。

# 6. 安装并运行 LVGL（lv_port_linux）

## 6.1 依赖

```bash
sudo apt install \
  build-essential cmake python3 python3-venv ninja-build \
  libsdl2-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols \
  libx11-dev \
  libdrm-dev libgbm-dev \
  libevdev-dev \
  libwebp-dev \
  libegl-dev libgles-dev libgl-dev
```

## 6.2 克隆

```bash
git clone -b release/v9.5 --recursive https://github.com/lvgl/lv_port_linux.git
cd lv_port_linux
```

## 6.3 修改 `lv_conf.defaults`

```bash
nano lv_conf.defaults
```

确保包含（或等价配置）：

```text
LV_COLOR_DEPTH               32
LV_USE_LINUX_FBDEV           0
LV_USE_LINUX_DRM             1
LV_USE_LINUX_DRM_GBM_BUFFERS 0
LV_USE_EVDEV                 1
LV_USE_DEMO_WIDGETS          1
```

## 6.4 确认 DRM / 触摸节点

DRM（找 `status` 为 `connected` 的 DSI）：

```bash
for file in /sys/class/drm/card*-*/status; do echo "$file -> $(cat $file)"; done
```

示例：`/sys/class/drm/card2-DSI-2/status -> connected` → 设备节点多为 **`/dev/dri/card2`**（以实机为准）。

触摸：

```bash
cat /proc/bus/input/devices
```

查找本触摸芯片名称，在 `Handlers=` 中取 `eventN`，例如 `event5` → **`/dev/input/event5`**（以实机为准）。

## 6.5 替换 `src/main.c`

将本目录 `main.c` 拷到 `lv_port_linux/src/main.c`。若实机 DRM/触摸节点不是 `card2` / `event5`，请改 `main.c` 里：

- `LV_LINUX_DRM_CARD`
- `LV_LINUX_EVDEV_POINTER_DEVICE`

（默认分辨率已按 **800×1280** 配置。）

## 6.6 若构建未生成 `lv_conf.h`

参见上游说明：<https://github.com/lvgl/lv_port_linux/issues/127>  
可在 CMake 中确保：

```cmake
find_package(Python3 REQUIRED COMPONENTS Interpreter)
```

## 6.7 编译与运行

```bash
cd lv_port_linux
cmake -B build -GNinja
cmake --build build
./build/bin/lvglsim
```

# 7. 开机自启（可选，systemd）

```bash
sudo nano /etc/systemd/system/lvgl.service
```

按实际用户与路径修改：

```ini
[Unit]
Description=LVGL Application Service
After=multi-user.target
Wants=multi-user.target

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/lv_port_linux
ExecStart=/home/pi/lv_port_linux/build/bin/lvglsim
Restart=always
RestartSec=3
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```

```bash
sudo usermod -aG render,video,input pi
sudo systemctl daemon-reload
sudo systemctl start lvgl.service
sudo systemctl status lvgl.service
# 确认正常后再：
sudo systemctl enable lvgl.service
```

常用：

```bash
sudo journalctl -u lvgl.service -f
sudo systemctl stop lvgl.service
sudo systemctl disable lvgl.service
```
