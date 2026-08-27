# 1. 准备工作

```
# 更新软件包列表
sudo apt update

# 安装编译工具链与匹配的内核头文件
sudo apt install build-essential linux-headers-$(uname -r)

# 创建文件夹并进入
mkdir vc4-kms-dsi-jd9366-touch && cd vc4-kms-dsi-jd9366-touch
```

# 2. 驱动源码（panel-jd9366-800x1280.c）

```
sudo nano panel-jd9366-800x1280.c
```

# 3. 驱动源码（jd9366_touch.c）

```
sudo nano jd9366_touch.c
```



# 4. Makefile

```
sudo nano Makefile
```

```
obj-m += panel-jd9366-800x1280.o
obj-m += jd9366_touch.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
```

> 编译：

```
make clean
make
```

```
sudo mkdir -p /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/panel/
sudo cp panel-jd9366-800x1280.ko /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/panel/

sudo mkdir -p /lib/modules/$(uname -r)/kernel/drivers/input/touchscreen/
sudo cp jd9366_touch.ko /lib/modules/$(uname -r)/kernel/drivers/input/touchscreen/

sudo depmod -a
```

# 5. 设备树 Overlay（vc4-kms-dsi-jd9366-touch.dts）

```
sudo nano vc4-kms-dsi-jd9366-touch.dts
```

> 编译并安装：

```
dtc -I dts -O dtb -o vc4-kms-dsi-jd9366-touch.dtbo vc4-kms-dsi-jd9366-touch.dts

sudo cp vc4-kms-dsi-jd9366-touch.dtbo /boot/firmware/overlays/
```


# 5. 启用

> 编辑 /boot/firmware/config.txt，添加：

```
sudo nano  /boot/firmware/config.txt
```



```
# 关闭自动检测，避免和手动 overlay 冲突
display_auto_detect=0

dtoverlay=vc4-kms-v3d

dtoverlay=vc4-kms-dsi-jd9366-touch

# 忽略官方 LCD
ignore_lcd=1
```

> 重启：

```
sudo reboot
```



# 6.关闭桌面（如果有就操作，没有就跳过这个步骤）



## 6.1_第一步：关闭系统桌面服务

在 Raspberry Pi 5 官方系统（Bookworm 及以上）中，默认使用的是基于 Wayland 的桌面合成器（Labwc 或 Wayfire）及 LightDM 登录管理器。

1. 打开终端，使用命令行配置工具：

   ```
   sudo raspi-config
   ```

  2.依次选择：

- **`1 System Options`** -> **`S5 Boot / Auto Login`**
- 选择 **`B1 Console`** 或 **`B2 Console Autologin`**（推荐选 Autologin，即开机自动登录到命令行控制台）。

 3.退出 `raspi-config` 并重启系统：

```
sudo reboot
```

重启后，树莓派将直接停留在命令行，桌面合成器不再运行，不会占用 DRM 显卡资源。



## 6.2_第二步：添加运行权限与组配置

在纯命令行下运行 DRM 程序或读取输入设备，用户需要拥有 `video` 和 `input` 组的权限：

```
sudo usermod -aG video,input $USER
```

(配置后建议重新登录或重启生效)

# 7.下载编辑运行LVGL

## 7.1_安装必要软件

```
# Debian / Ubuntu
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



## 7.2_下载lv_port_linux

```
# 克隆指定分支并拉取子模块
git clone -b release/v9.5 --recursive https://github.com/lvgl/lv_port_linux.git
cd lv_port_linux
```



## 7.3_修改lv_conf.defaults文件配置

```
sudo nano lv_conf.defaults
```



```
/* 1. 设置色深为 32 */
LV_COLOR_DEPTH	    32

/* 2. 关闭 FBDEV 驱动 */
LV_USE_LINUX_FBDEV       0

/* 3. 开启 DRM 驱动 */
LV_USE_LINUX_DRM         1
LV_USE_LINUX_DRM_GBM_BUFFERS 0

/* 4. 开启 Evdev 触摸驱动 */
LV_USE_EVDEV             1

/* 5. 开启 Demo 支持 */
LV_USE_DEMO_WIDGETS      1
```

## 7.4_查找树莓派 DRM 屏幕设备节点

```
for file in /sys/class/drm/card*-*/status; do echo "$file -> $(cat $file)"; done
```

在输出路径中，根据 `status` 为 **`connected`** 的行提取设备节点：

```
/sys/class/drm/card1-HDMI-A-1/status -> disconnected
/sys/class/drm/card1-HDMI-A-2/status -> disconnected
/sys/class/drm/card1-Writeback-1/status -> unknown
/sys/class/drm/card1-Writeback-2/status -> unknown
/sys/class/drm/card2-DSI-2/status -> connected
```

**输出示例**：`/sys/class/drm/card2-DSI-2/status -> connected`

**设备节点**：路径中的 `card2` 对应屏幕设备节点 **/dev/dri/card2**

## 7.5_查看触摸设备节点

```
cat /proc/bus/input/devices
```

在输出结果中查找你的触摸屏芯片名称（例如 `jd9366_touch` 或 `cst820`），找到对应的 **`Handlers`** 行：

```
I: Bus=0018 Vendor=0000 Product=0000 Version=0000
N: Name="jd9366_touch"
P: Phys=i2c/jd9366_touch
S: Sysfs=/devices/platform/axi/1000120000.pcie/1f00080000.i2c/i2c-11/11-0068/input/input5
U: Uniq=
H: Handlers=event5 
B: PROP=2
B: EV=b
B: KEY=400 0 0 0 0 0
B: ABS=2e0800000000000
```

在上例中，`Handlers=event5` 即说明触摸屏对应的设备节点为 **/dev/input/event5**。



## 7.6_修改main.c

```
sudo nano main.c
```

## 7.7_修复未能产生lv_conf.h问题

```
https://github.com/lvgl/lv_port_linux/issues/127
```

```
find_package(Python3 REQUIRED COMPONENTS Interpreter)
```

## 7.8_编译和运行

> 编译

```
cd lv_port_linux

cmake -B build -GNinja
cmake --build build
```

> 运行

```
./build/bin/lvglsim
```

# 8.开机运行lvgl

在树莓派 (Raspberry Pi OS) 上实现开机自动运行 LVGL 程序，最稳定、标准的方法是使用 **systemd 服务**。这样不仅可以在系统启动时自动加载，还能在程序异常崩溃时自动重启。

以下是完整的配置步骤：

### 第一步：创建 systemd 服务文件

在终端中执行以下命令，创建一个名为 `lvgl.service` 的服务配置：

```
sudo nano /etc/systemd/system/lvgl.service
```

在打开的文件中粘贴以下内容（注意根据你的实际路径确认可执行文件和目录）：

```
[Unit]
Description=LVGL Application Service
After=multi-user.target
Wants=multi-user.target

[Service]
Type=simple
# 你的树莓派用户名，通常为 pi
User=pi
# 你的工作目录路径
WorkingDirectory=/home/pi/lv_port_linux
# 可执行文件的绝对路径
ExecStart=/home/pi/lv_port_linux/build/bin/lvglsim
# 如果崩溃自动重启
Restart=always
RestartSec=3
# 确保 DRM/TTY 控制台输出正常
StandardOutput=journal
StandardError=journal

[Install]
WantedBy=multi-user.target
```



### 第二步：给予权限与重载配置

因为 DRM 节点 `/dev/dri/card2` 和输入节点 `/dev/input/event5` 需要相关用户组权限，确保 `pi` 用户在 `render`, `video` 和 `input` 组中：

```
sudo usermod -aG render,video,input pi
```



重新加载 systemd 管理配置：

```
sudo systemctl daemon-reload
```

### 第三步：测试服务运行

在设置开机自启前，先手动启动服务测试是否正常显示：

```
# 启动服务
sudo systemctl start lvgl.service

# 查看服务状态（查看是否有报错）
sudo systemctl status lvgl.service
```

如果屏幕正常显示 LVGL 界面且没有任何报错，可以停止测试：

```
sudo systemctl stop lvgl.service
```

### 第四步：开启开机自启

测试无误后，执行以下命令开启开机自动运行：

```
sudo systemctl enable lvgl.service
```

此时重启树莓派即可验证：

```
sudo reboot
```

### 实用维护命令小结

- **查看日志/排查报错**：

```
sudo journalctl -u lvgl.service -f
```

- **临时停止开机自启程序**：

```
sudo systemctl stop lvgl.service
```

- **禁用开机自启**：

````
sudo systemctl disable lvgl.service
````

