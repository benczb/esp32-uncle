[English](README.md) · **简体中文**

# Opus 回复语音解码器

定点 Opus 1.6.1 和 Passport 包装器复制自
[FalkoWing/muse-passport](https://github.com/FalkoWing/muse-passport)，提交
`664f5c2a3c9fb9c0b0bf4ed9801907c7f01fdb3e` 的 `shared/opus`。
源码及版权声明保持原样，见 [COPYING](COPYING)。仅调整 CMake 路径。
启用 Muse 的应用沿用上游 16 kHz 单声道、60 毫秒、120 字节数据包解码器，
未使用的编码器代码由链接器移除。解码器内存和播放栈仅在朗读时存在。
