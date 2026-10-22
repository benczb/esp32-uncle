[English](README.md) · **简体中文**

# 随身护照电脑端

电脑与 Passport 连接同一局域网。需要 Python 3.10+ 和 `ffmpeg`，无需额外 Python
依赖包。电台、Athena 和助手请求需要电脑持续运行；公开天气、PSI、巴士数据现由固件直接获取。头像和资料保存在设备中。

```bash
python3 companion/start_local.py --bind <电脑局域网IP> \
  --hermes-url http://127.0.0.1:8642/v1 \
  --hermes-env /path/to/hermes/.env
```

首次运行自动创建私有文件 `companion/.local/bridge-token`。在设备的手机设置页填写
该令牌和 `http://<电脑局域网IP>:4188`。启动器仅从指定 Hermes `.env` 读取
`API_SERVER_KEY`，不输出密钥、不改动 Hermes 配置。默认连接本地兼容 OpenAI 的
Hermes API。Muse 指官方 Muse AI 账号与 Muse Passport 手机伴侣；已停用同名本地模型路线，蓝牙协议集成尚未完成。

也可直接运行 `server.py`，设置 `PASSPORT_BRIDGE_TOKEN`（至少 16 字符）、
`HERMES_URL`、`HERMES_TOKEN`、`HERMES_MODEL`、`ATHENA_URL`。助手 URL 以 `/v1` 结尾；Athena 地址是现有 4177
服务根地址。默认只监听本机；连接实体设备时绑定可信局域网接口，不要把 HTTP 服务
发布到互联网。局域网 HTTP 传输包含访问令牌，应使用可信 Wi-Fi。

## API

所有路由要求 `Authorization: Bearer <bridge-token>`。

| 路由 | 用途 |
| --- | --- |
| `GET /v1/status` | 时间、电台数量、助手配置标志 |
| `GET /v1/athena` | 提供方、状态、额度；不返回邮箱和凭据路径 |
| `GET /v1/weather?city=singapore` | 室外天气、十分钟缓存、明确的过期标志 |
| `GET /v1/haze?city=singapore&region=central` | 最新 NEA 24 小时 PSI；手选区域或 `region=auto&lat=...&lon=...`，含位置/过期/覆盖状态 |
| `GET /v1/stations` | 六个新加坡及四个马来西亚电台 |
| `GET /v1/radio?station=0` | 16 kHz、有符号小端 16 bit、单声道 PCM |
| `POST /v1/chat/hermes` / `muse` | JSON `{ "text": "..." }`，异步提交 |
| `GET /v1/reply/hermes` / `muse` | 状态 `idle`、`working`、`done`、`error` 和文字 |
| `POST /v1/voice/start` | 开始 Hermes 录音上传：JSON 包含 `agent`、`sample_rate:16000`、`channels:1`、`format:"s16le"`，返回随机录音 ID |
| `POST /v1/voice/chunk?session=...&seq=0` | PCM 分块长度为偶数，最多 4,096 字节；按序提交，完全相同的上一分块可安全重试 |
| `POST /v1/voice/finish` / `cancel` | JSON 包含 `session`；结束后异步本地转写并发送 Hermes，取消则丢弃未提交录音 |

电脑负责电台解码，最多两个并行音频流。每个助手只允许一个进行中请求。设备端及
设置请求都有大小限制，不向客户端暴露上游错误正文。回复保存在内存中，不是持久
聊天记录。消息可从手机设置页输入；Hermes 也可通过原生电脑桥接接收麦克风提问，回复可通过下述本地 eSpeak NG 后端在 Passport 扬声器朗读。

### 回复朗读

电脑桥接需安装 `espeak-ng` 和 `ffmpeg`（Ubuntu 可执行
`sudo apt install espeak-ng ffmpeg`），无需付费服务或 API 密钥。
`PASSPORT_ESPEAK` 可指定可执行文件路径。中文回复使用普通话，其他回复使用美式
英语。文字通过标准输入传递，不放入命令行或 shell。合成结果及最近一条回复缓存
仅存内存，不保存语音文件。eSpeak NG 是单独安装的 GPL 程序，本仓库不包含其源码。

Hermes 完成的回复包含 `speech_id`；通过认证的
`GET /v1/speech/hermes?id=<speech_id>` 返回该条当前回复的 16 kHz 单声道小端
16 位 PCM，旧 ID 会被拒绝。设备反复轮询不会重复朗读同一 ID。手机浏览器转发使用
现有的有界 PCM 队列传送音频。在设备按确定打开音量，上下键每次调节 10%，再次
确定关闭；面板外按住下键录音，长按确定离开并停止播放。Muse 朗读走官方 Android
伴侣，不经过此接口。

### 本地语音后端

在 Hermes 页面按住下键，看到“正在录音”后说话，松开下键发送。长按确定离开会取消尚未提交的录音。原生电脑桥接录音最多 30 秒；Android 麦克风上传转发与官方 Muse 语音尚待完成。
Muse 语音使用官方连接，不走 Hermes 的转写路由。

在桥接服务的 Python 环境执行 `python3 -m pip install -r companion/requirements-voice.txt`
安装可选依赖。首次准备模型：
`python3 -c "from faster_whisper import WhisperModel; WhisperModel('small.en', device='cpu', compute_type='int8')"`。
运行时请求仅加载已缓存模型，不自动下载。默认 `small.en` 识别英语；需要多语言时，
预先准备多语言模型，在桥接环境设置 `PASSPORT_STT_MODEL` 和
`PASSPORT_STT_LANGUAGE=auto` 后重启服务。

录音仅保存在内存中，最长三十秒，格式为 16 kHz 单声道 PCM。结束或取消时丢弃上传
缓存；采集超过 35 秒后，在下一次请求中清理。一次只接受一段录音，期间阻止文字
Hermes 请求插入。结束后将识别文字提交给已配置的 Hermes；已提交的消息不能撤回。

天气通过 Open-Meteo 支持新加坡、新山、吉隆坡、槟城，显示室外温度、湿度、天气和
风速，不使用实体天气传感器。电台预置 YES 933、LOVE 972、CAPITAL 958、CLASS 95、
GOLD 905、987、MY FM、MELODY、HITZ、ERA。公开音频源可能变更或失效；
修改 `server.py` 中的目录即可更新电台，无需重新刷机。

## 检查

```bash
python3 tests/test_passport_companion.py
python3 tests/test_passport_haze.py
python3 tests/test_passport_settings.py
./tools/render_passport.sh  # 固件依赖下载完成后运行
```

原生渲染测试检查实际 LVGL 字体描述符，并在 24 KB 内存池下渲染两套主题。
软件渲染的 PPM 帧输出到 `build/screens/`，不是已刷机设备的实拍画面。

## 巴士到站：LTA DataMall

巴士到站直接使用官方 **LTA DataMall v3 Bus Arrival API**。
设备通过自身网络联网，此功能不需要电脑桥接；不会回退调用 Bus Aunty 托管 API。

1. 申请自己的 [DataMall Account Key](https://datamall.lta.gov.sg/content/datamall/en/request-for-api.html)。
2. 打开 Passport 手机设置网页，输入当前六位设备设置码，等待读取已保存资料。
3. 在 **LTA API 密钥**填写密钥，点击**保存设置**。以后编辑时留空会保留已有密钥，
   已保存密钥不会回传给浏览器。
4. 在首页按**下键**进入巴士到站，再按**确定**刷新。提示缺少密钥时请完成第 3 步；
   密钥被拒绝表示 LTA 未批准该次请求。此密钥与电脑桥接访问令牌不同。

默认收藏 **Eunos Int（82009）**。手机页面最多保存十个收藏站点，
每行填写 `站点代码 | 名称`，保留前导零。每页四行，显示服务号和最多三班到站分钟数。
**Arr** 表示即将到站，**60+** 表示超过一小时，**--** 表示不可用。
目的地显示为 **To stop 54009** 等站点代码；此构建没有从 LTA 独立的 Bus Stops
数据集读取目的地名称。不同目的地和访问次数保持独立，不会合并。

上下键翻页和切换收藏站，确定刷新，长按确定返回菜单。
请求缓存六十秒，失败后按 120/240/300 秒退避。界面更新时间表示本次成功**获取时间**，
不是 LTA 来源时间。刷新失败或数据超过三分钟会标记过期，并隐藏倒计时。

可选的电脑桥接巴士路由需要在桥接进程的私人环境中设置 **`LTA_ACCOUNT_KEY`**，
再重启桥接。这与保存在 Passport 上的密钥相互独立。
不要把真实密钥写入受 Git 跟踪的文件、命令行参数、固件默认值或截图。
`GET /v1/bus?stop=82009&page=0` 仍需正常的桥接 Bearer 鉴权。
桥接端缺少密钥时返回 `lta_key_required`，不会改用其他运营方服务。

数据来源：**新加坡陆路交通管理局（LTA）**，通过
[LTA DataMall](https://datamall.lta.gov.sg/) 获取，遵循
[Singapore Open Data Licence](https://data.gov.sg/open-data-licence) 与
[LTA API 条款](https://datamall.lta.gov.sg/content/datamall/en/api-terms-of-service.html)。
[API 文档](https://datamall.lta.gov.sg/content/dam/datamall/datasets/LTA_DataMall_API_User_Guide.pdf)。
本项目独立开发，不代表 LTA 背书；到站预测可能变化。
此前参考的 [Bus Aunty Mini](https://github.com/awesomelionel/bus-aunty-mini)
及其 MIT 版权声明继续保留。

## 可选 Android 手机转发

如需手机浏览器通过 Tailscale 转发 Hermes、用量与广播请求，请明确监听两个接口，并启用可选 CORS：

```bash
python3 companion/start_local.py --bind <电脑局域网IP> \
  --bind <电脑TailscaleIP> --phone-relay \
  --hermes-env /path/to/hermes/.env
```

CORS 仅接受设备设置页的 HTTP、RFC1918 私有 IPv4 字面地址来源。
所有数据接口仍须 Bearer 验证；OPTIONS 仅处理预检，不使用 Cookie。
不要监听公网接口或进行公网端口转发。Tailscale 访问规则须允许手机访问 TCP 4188。
Android 热点客户端不会自动共享手机 VPN；由手机浏览器请求桥接，再通过热点局域网
将回复和 PCM 音频发回设备。点击设置页的**连接手机转发**启用用量和广播，
Hermes 消息使用转发复选框。请保持页面在前台、手机保持唤醒；八秒无心跳时停止设备播放。浏览器限制与真实 Android 行为仍需设备验收。
固件现可直接获取公开巴士、PSI、天气；广播与 Athena 仍使用此桥接服务。

Athena 默认地址为 `http://127.0.0.1:4177`。如实际看板监听其他接口，请在启动前设置 `ATHENA_URL` 为实际根地址（不含 `/api/usage`）。

### 仅运行当前功能

设备仅运行当前选中的功能。离开收音机会停止音频流并释放编解码器、DMA 和手机转发缓冲区；离开助手会取消尚未提交的录音并停止轮询。旧功能的排队任务会被丢弃，已保存的资料仍保留。头像显示缓冲区由工作任务在主页或名片页从闪存读取，离开这些页面后释放；已保存的头像仍保留。助手显示缓冲区在退出时释放，重新打开助手会从桥接服务再次读取最新回复。使用手机设置页发送消息前，请先在设备打开相应助手页面。

Wi-Fi 和设置网页服务保持可用。天气约每两分钟在后台刷新一次；录音期间会暂缓网络任务，以适应内部 RAM 限制。蓝牙作为共享连接服务保留，但官方 Muse 蓝牙集成仍待完成。已经提交到电脑的请求无法通过离开页面撤回，也不会停止电脑上的 Hermes 服务。

## 在设置时连接用量账号

账号设置需要电脑运行 [Athena Usage Tracker](https://github.com/luckeyfaraday/athena-usage-tracker)。安装其 Node.js 依赖后，在该项目目录使用桥接服务默认的用量端口启动：

```bash
HOST=127.0.0.1 PORT=4177 npm start
```

如使用其他地址，在启动本伴侣前设置 `ATHENA_URL`。按需安装官方 [Codex CLI](https://learn.chatgpt.com/docs/auth) 和 [Claude Code](https://code.claude.com/docs/en/authentication)。

1. 打开 Passport 设置网页，填写桥接服务地址和服务访问令牌。
2. 在 **AI 用量账号登录** 中选择 **ChatGPT / Codex** 或 **Claude Code**，填写名称并点击 **添加用量账号**。
3. 选择账号并点击 **连接所选账号**。OpenAI 用户打开官方页面、输入显示的一次性登录码；必要时在 ChatGPT 安全设置启用设备码登录。Claude 用户请在桥接电脑上完成官方 Claude Code 浏览器登录，手机无法访问电脑的 localhost 回调。
4. 看到 **账号已连接** 后，在 Passport 打开 AI 用量并按确定刷新。**取消本次登录** 会停止尚未完成的尝试，不会退出已经连接的账号。

OpenAI 登录连接 ChatGPT 账号以读取 Codex 配额窗口及重置时间，不代表可以读取 ChatGPT 网页聊天的独立消息额度。Claude 登录由未修改的官方 Claude Code CLI 完成，本应用不接收提供商密码或令牌。Claude 配额仍依赖用量追踪器的官方 CLI 状态栏或同步数据，因此仅登录成功时，配额仍可能暂不可用。

每个新账号使用被 Git 忽略的独立 CLI 目录 `companion/.local/usage-accounts/<id>/`。注册表仅保存显示信息及追踪器账号 ID，不包含提供商令牌；原生 CLI 凭证由 CLI 保存在私有目录。登录网址和一次性代码仅在内存中保存，完成或取消后不再返回。账号设置接口需要桥接访问令牌，不接收文件系统路径或 shell 命令，最多支持六个新设置账号；现有追踪器账号保留。

接口：`GET/POST /v1/accounts`、`GET /v1/accounts/<id>/status`、`POST /v1/accounts/<id>/login` 和 `/cancel`。创建账号时向配置的追踪器注册独立目录；登录使用官方 [Codex app-server 设备码流程](https://learn.chatgpt.com/docs/app-server) 或 [Claude 官方登录流程](https://code.claude.com/docs/en/authentication)。

如果桥接服务可访问但 AI 用量读取失败，请另行检查用量跟踪器地址。`ATHENA_URL` 必须与跟踪器实际监听地址一致，默认是 `http://127.0.0.1:4177`。私有启动器在未设置 `ATHENA_URL` 时，也会读取可选的 `companion/.local/usage-url` 本地地址文件；明确设置的环境变量优先。该文件仅保存在本机，并由 Git 忽略。

## Spotify 与书架

在伴侣服务打开 `/tools`，连接 Spotify 或上传 UTF-8 TXT、无 DRM 的 EPUB。
页面要求输入现有桥接令牌，不将其存入浏览器存储。
Spotify 需要 `SPOTIFY_CLIENT_ID`、已登记的回调地址（默认
`http://127.0.0.1:4188/spotify/callback`）、Premium 和活动播放器。
`SPOTIFY_REDIRECT_URI` 可修改回调地址；令牌仅保留在伴侣电脑。

已认证接口：`GET /v1/spotify`、`POST /v1/spotify/login`、
`POST /v1/spotify/control`（`previous`、`next`、`toggle`）；
`GET /v1/library`、`POST /v1/library/import?name=book.epub`（文件原始字节）、
`GET /v1/reader`、`POST /v1/reader/control`（`previous`、`next`、`open`、`size`、
`shelf`）。控制请求为带 `action` 字段的 JSON 对象。
只有 `/tools` 和通过 state 验证的 `/spotify/callback` 不需要 bearer 认证。

限制、保存路径、设备操作及待完成的实时测试见
[导航、Spotify 和阅读](../docs/applications/pocket-passport.zh_CN.md#导航spotify-和阅读)。
Spotify 控制和书籍翻页需要持续连接伴侣服务。Komoot 导航通过手机直接 BLE 连接设备。

### 简单的手机设置

1. 保持电脑桥接服务运行。手机与 Passport 连接同一个家庭 Wi-Fi，桥接服务监听电脑局域网地址。
2. 从 Passport 手机设置页打开 **Spotify / Books setup**。在 `/tools` 输入原有服务令牌，点击 **Connect companion**。
3. 选择 TXT/EPUB，点击 **Add book**，然后点击书名旁的 **Read on Passport**。
   在 Passport 打开 **Books**，即可从所选图书的已保存页码继续阅读。

页面会检查身份验证、说明连接故障并预览当前页面。图书与 Spotify 仍需要设备直接访问电脑桥接服务；
Android 浏览器中继不转发这些接口。它们并非只需手机的独立服务。导航通过 Komoot BLE 直连，不需要电脑。

使用 Spotify 默认回环回调地址时，先在电脑浏览器完成一次登录，再通过手机或设备控制音乐。
启动桥接服务时同时监听 `127.0.0.1` 和局域网地址（重复使用 `--bind`），以接收回调。
手机登录需要已注册、可到达桥接服务的 HTTPS 回调；普通 HTTP 局域网回调不符合
[Spotify 回调规则](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri)。
当回调要求使用电脑时，手机页面会禁用登录按钮。

数据接口需要 `Authorization: Bearer <bridge-token>`；静态 `/tools` 页面与经过 state 校验的 Spotify 回调除外。
`GET /v1/setup` 返回需要身份验证的 Spotify 设置状态，不含凭据。
`POST /v1/reader/control` 新增 `select` 操作，并通过书架中的 `id` 选择具体图书；
原有 `previous`、`next`、`open`、`size`、`shelf` 操作保持兼容。
