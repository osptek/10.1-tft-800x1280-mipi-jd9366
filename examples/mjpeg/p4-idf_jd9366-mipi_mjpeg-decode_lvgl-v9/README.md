```
ffmpeg -i input.mp4 -vf "scale=800:1280:force_original_aspect_ratio=decrease,pad=800:1280:(ow-iw)/2:(oh-ih)/2:black" -c:v mjpeg -q:v 5 -f mjpeg mjpeg_800_1280_30fps.mjpeg
```

# 注意事项

>+ 启动PSRAM

>+ 宽和高是16倍数

# 支持长文件名
```
FATFS_LONG_FILENAMES=CONFIG_FATFS_LFN_HEAP
FATFS_MAX_LFN=255
```