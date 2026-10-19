# ESP32 Uncle 构建说明

[English](esp32superapp-build.md)

## 公开源码快照

本仓库以一个新的初始提交开始，包含经过审阅的当前 Pocket Passport 应用。
不包含旧仓库历史、对话日志、个人展示媒体、私人设备配置、备份或编译固件。
请使用自己的配置和账号。

## 构建与安装

先读[开始使用](../../README.zh_CN.md)和[ESP-IDF 环境指南](../development/engineering/environment-setup.zh_CN.md)。
使用 ESP-IDF 5.5.3，运行 `./tools/validate.sh` 完成仓库检查、主机测试、构建及合并镜像校验。
目标为 ESP32-C3、8 MB、无 PSRAM。[构建指南](../development/engineering/build-and-test.zh_CN.md)
说明可选 Muse 配置及匹配固件/调试产物。[应用指南](pocket-passport.zh_CN.md)介绍功能与按键，
[电脑伴侣指南](../../companion/README.zh_CN.md)介绍服务及账号设置。

合并镜像烧录可能重置保存的数据。已有设备请遵守[数据保存政策](../development/engineering/firmware-layout.zh_CN.md#烧录与已保存数据)，
需要保留设置时只能使用分区兼容的分段镜像。公开快照不包含维护者个人固件或设置。

## 项目来源

- [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)：板级支持、平台与开发参考。
- [Muse Passport](https://github.com/FalkoWing/Muse-passport)：官方 Muse Android 伴侣集成及语音参考。
- [Folo AI Passport Voice](https://github.com/manchunx7-bit/folo-ai-passport-voice)：语音交互参考。
- [Tokens Kanban](https://github.com/GreentailTohru-arch/Tokens-Kanban)：用量跟踪参考。
- [Bus Aunty Mini](https://github.com/awesomelionel/bus-aunty-mini)：巴士显示参考，正式到站数据使用 LTA DataMall。

分发时保留[根许可证](../../LICENSE)、Muse 与 Opus 声明及[素材声明](../../assets/README.zh_CN.md)。
第三方服务需使用自己的账号/密钥，并遵守各自条款。
