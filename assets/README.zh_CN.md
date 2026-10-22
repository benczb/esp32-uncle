<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 随身护照字体

设备英文界面统一使用 LVGL Montserrat 16 像素字体。局部字体描述符以
`passport_font_16` 作为中文回退，混合文字仍可显示；中文模式保留该生成的
16 像素字体。标签、数值和按键提示共用同一英文字体，不再单独缩小组件字号。


`fonts/passport_font_16.c` 合并 DejaVu Sans Mono 的 ASCII/标点与经过源字体检查的
Droid Sans Fallback GB2312 子集（16 像素、2 bpp、不压缩）。
`fonts/passport-symbols.txt` 是精确支持字符表；动态内容中不支持的字符显示为 `?`。
使用 `lv_font_conv@1.5.3`，执行 `python3 tools/generate_passport_font.py` 可重新生成，
源字体路径记录在脚本中。许可证见 `fonts/NOTICE.txt`、`fonts/Apache-2.0.txt`
及 `fonts/DejaVu-copyright.txt`。C 源码由 `main/CMakeLists.txt` 编译一次。
原生 LVGL 测试检查完整字符表和一个应当缺失的 emoji；设备实际显示仍需单独验收。

## 公开媒体

本公开快照不包含个人首页照片、私人简介视频及其编辑源码。通用硬件/品牌图片及字体许可证声明保留。

`licenses/bus-aunty-mini-MIT.txt` 保留巴士参考项目的完整
`Copyright (c) 2026 awesomelionel` MIT 声明。

## 公开屏幕画廊

[`gallery/`](gallery/README.zh_CN.md)包含 48 张 HyperFrames 图片（每种语言各 24 个视图）、
可编辑源文件及可滑动画廊，原有的 [`images/pocket-uncle-cover.png`](images/pocket-uncle-cover.png)
仍放在第一张。所有者明确授权在首页使用所提供的插画头像，并展示公开领英二维码；
私人联系二维码、电话号码、设备标识和人工智能账号详情均排除在外。
确切来源提交、尺寸、第三方声明及隐私边界见画廊的来源和复现说明。
头像源文件为已授权的插画，并非设备上的私人照片。这些预览不能作为固件验收证据。
