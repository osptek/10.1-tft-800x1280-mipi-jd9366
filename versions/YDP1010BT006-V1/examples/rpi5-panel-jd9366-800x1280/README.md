# 1. 准备工作

```
sudo apt update
sudo apt install raspberrypi-kernel-headers build-essential device-tree-compiler
mkdir jd9366-800x1280 && cd jd9366-800x1280
```

# 2. 驱动源码（panel-jd9366-800x1280.c）

```
sudo nano panel-jd9366-800x1280.c
```



# 3. Makefile

```
sudo nano Makefile
```



```
obj-m += panel-jd9366-800x1280.o

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

# 4. 设备树 Overlay（vc4-kms-dsi-jd9366-800x1280.dts）

```
sudo nano vc4-kms-dsi-jd9366-800x1280.dts
```



> 编译并安装：

```
dtc -@ -I dts -O dtb -o vc4-kms-dsi-jd9366-800x1280.dtbo vc4-kms-dsi-jd9366-800x1280.dts
sudo cp vc4-kms-dsi-jd9366-800x1280.dtbo /boot/firmware/overlays/
sudo cp panel-jd9366-800x1280.ko /lib/modules/$(uname -r)/kernel/drivers/gpu/drm/panel/
sudo depmod -a
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

dtoverlay=vc4-kms-dsi-jd9366-800x1280

# 忽略官方 LCD
ignore_lcd=1
```

> 重启：

```
sudo reboot
```

