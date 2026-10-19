[English](README.md) · **简体中文**

<h1 align="center">🟢 ESP32 Uncle</h1>
<p align="center"><b>你的口袋，你的配置。</b><br>为 FoloToy AI Passport 打造的私人随身看板。</p>

<p align="center">
  <a href="docs/applications/pocket-passport.zh_CN.md"><img alt="使用指南" src="https://img.shields.io/badge/READ_THE_GUIDE-35C996?style=for-the-badge&amp;logo=readthedocs&amp;logoColor=white"></a>
  <a href="docs/applications/esp32superapp-build.zh_CN.md"><img alt="构建笔记" src="https://img.shields.io/badge/BUILD_NOTES-8766EB?style=for-the-badge&amp;logo=markdown&amp;logoColor=white"></a>
  <a href="companion/README.zh_CN.md"><img alt="连接桥接" src="https://img.shields.io/badge/CONNECT_THE_BRIDGE-E17450?style=for-the-badge&amp;logo=python&amp;logoColor=white"></a>
</p>

<p align="center">
  <img alt="ESP32 C3" src="https://img.shields.io/badge/ESP32--C3-8_MB_Flash-254D43?style=flat-square&amp;logo=espressif&amp;logoColor=white">
  <img alt="ESP IDF 5.5.3" src="https://img.shields.io/badge/ESP--IDF-5.5.3-336EAD?style=flat-square">
  <img alt="中文 LVGL 界面" src="https://img.shields.io/badge/UI-LVGL_%2B_Chinese-7954B3?style=flat-square">
  <a href="LICENSE"><img alt="MIT 许可" src="https://img.shields.io/badge/license-MIT-35A77B?style=flat-square"></a>
</p>

## 配置你自己的 Passport

使用自己的资料、照片、Wi-Fi 网络、常用巴士站和服务账号。
以下说明不依赖维护者的个人配置，先阅读[应用指南](docs/applications/pocket-passport.zh_CN.md)。

把它当作小型信息看板：**Passport** 负责显示，**Android 手机**提供热点与设置，
**私有电脑桥接**处理助手、用量与广播解码。

## 🚀 从这里开始

1. 先读[应用指南](docs/applications/pocket-passport.zh_CN.md)。
2. 换照片：手机连接 `Pocket-Passport`，打开 `http://192.168.4.1`，
   输入当前设置码，选择并裁剪照片，再保存新头像。
3. 按[电脑桥接说明](companion/README.zh_CN.md)配置助手、用量和广播。
4. 在设置页的 **AI 用量账号登录** 中连接自己的 ChatGPT/Codex 或 Claude 账号，详见[登录设置说明](companion/README.zh_CN.md#在设置时连接用量账号)。
5. 参见[公开快照构建笔记与上游致谢](docs/applications/esp32superapp-build.zh_CN.md)。

### 从源码安装

需要仓库访问权限、FoloToy AI Passport、USB **数据线**与 **ESP-IDF 5.5.3**。
不要使用其他 ESP32 板型的固件。

```bash
git clone https://github.com/benczb/esp32-uncle.git
cd esp32-uncle
```

按[环境安装指南](docs/development/engineering/environment-setup.zh_CN.md)安装并激活 ESP-IDF。
需要在家/外出预设时，复制安全示例，只编辑私人副本：

```bash
umask 077
mkdir -p .local
cp config/wifi-profiles.example.json .local/passport-defaults.json
# 用自己的 2.4 GHz 网络信息编辑 .local/passport-defaults.json。
```

如要在手机上手动配置网络，可不创建 `.local/`。然后构建验证：

```bash
./tools/validate.sh
./tools/render_passport.sh
```

验证后的镜像为 `build/FoloToy-AI-Passport-full.bin`。空白设备或明确的完整刷新可按
[刷写指南](docs/development/engineering/firmware-layout.zh_CN.md)从 `0x0` 写入**合并镜像**。
已有设置可能被重置；要保留设置的升级需使用匹配应用镜像并核对兼容分区。
USB 连接不等于同意刷机。

不想本地安装工具链的授权协作者可运行 **Actions → Build firmware → Run workflow**，
任务完成后下载产物；干净构建不含个人 Wi-Fi 预设。

目标为 **ESP32-C3、8 MB Flash、无 PSRAM**，使用原 FoloToy BSP。
固件归档在本地生成，不提交到 Git。

## ✨ 小设备，九个应用

| 应用 | 内容 |
| --- | --- |
| 🚌 巴士 | 使用自己的 LTA API 密钥，查看自选收藏站点与三次到站时间 |
| 🚆 列车警报 | LTA 列车服务警报与中断行程详情，首页显示一行通知 |
| 📊 AI 用量 | 通过自己的用量看板，显示自己连接的 Codex / Claude 账号剩余额度与重置倒计时 |
| 💬 Hermes | 手机输入文字，设备分页阅读回复 |
| 🪄 Muse | 通过 Muse Passport 连接官方 Muse AI · 集成进行中 |
| 🪪 名片 | 姓名、头像与可配置社交链接二维码；确定键切换 |
| 📻 广播 | 六个新加坡台、四个马来西亚台 |
| 🌦️ 天气 | 新加坡、新山、吉隆坡、槟城 |
| ⚙️ 设置 | 中英文、名片二维码、资料、照片、Wi-Fi、城市、PSI 区域与站点 |

首页标题显示本地星期与 24 小时时间，例如 **MON 12:55**，每分钟更新。
**--- --:--** 表示时间尚未同步。首页也显示 **NEA 24 小时 PSI**。设备没有 GPS，
移动后要手动更换区域或更新保存的坐标。

## 🔊 助手回复与音量

Hermes 使用电脑桥接上的本地 eSpeak NG 合成语音，通过 Passport 扬声器播放；
电脑需安装 `espeak-ng` 和 `ffmpeg`。Muse 使用原版 Muse Passport Android
伴侣的蓝牙回复朗读功能，请在伴侣中启用语音并保持连接，固件需启用 Muse。
两者均无需额外语音 API 密钥。

在助手页面按 **确定** 打开音量面板，**上下键** 每次调节 10%，屏幕显示百分比，
**0% 为静音**。再次按确定返回回复，长按确定返回菜单。音量与收音机共用，当前
会话内有效。离开音量面板后，按住下键录音、松开发送。离开助手或开始录音会停止
播放。Hermes 需要电脑桥接，外出时回复音频还需保持手机浏览器转发连接。音频
不可用时仍可阅读文字回复。

## 📱 用手机完成设置

填写自己的显示名称、联系方式、头像和可选名片二维码链接。
输入自己的 2.4 GHz Wi-Fi 信息，选择支持的天气城市与 PSI 区域，
并将预填巴士收藏替换为自己的站点代码。助手和用量功能需要自己的桥接地址、
访问令牌和账号；巴士与列车数据需要自己的 LTA DataMall 密钥。
不要使用他人的凭据，也不要复制他人的私人固件配置。

先用 Passport 按键打开设置，再用手机浏览器编辑资料。设置网页不需要 USB
连接，也不需要另装手机应用。

编辑资料时，请让设备停留在**设置 → Wi-Fi 网络**，看到**设置已保存。**后再离开。
手机网页在 Wi-Fi 设置、Hermes 和 AI 用量页面可用；手机转发已连接时，经过广播、设置子菜单和菜单
也能保持连接。首页开启 `Pocket-Passport` 配网热点时也可打开网页。
在其他页面需要继续编辑时，请重新进入设置，再刷新手机浏览器。

设备直接播放广播或 Hermes 语音时，天气通过同一个私人电脑桥接获取，避免设备同时建立第二个 HTTPS 连接。
手机转发期间，请保持浏览器网页开启，手机会同时转发天气和音频。其他情况下，天气直接连接 Open-Meteo；直接请求失败（包括 TLS 内存不足）时，回退到已配置的电脑桥接。PSI 直接请求无法完成时也回退到桥接，沿用保存的区域或位置。

### 外观与设置子菜单

在菜单打开 **09 设置**，选择 **Wi-Fi 网络**进行手机网页配置，选择**语言**切换中英文、选择**亮度**调节背光，或选择
**外观**切换**荧光绿**、**白色**、**全彩**。上下键预览，按**确定**保存；
屏幕显示**已保存**后，重启仍保留。长按确定返回设置，再次长按确定返回菜单。
默认仍为荧光绿。白色采用黑底白字；全彩以蓝色区分导航、琥珀色标记选择。
菜单第一项为巴士到站，第二项为列车警报，个人名片紧接 Muse。

在**亮度**页面，上下键以 10% 为步长预览 **10% 至 100%** 背光，确定保存，
重启后仍保留。默认 70%。未保存就长按确定返回设置，会恢复上次保存的亮度。
最低亮度保留为 10%，避免屏幕全黑。

在**设置 → 语言**选择 **English** 或**中文**，按确定保存，长按确定返回设置。
Wi-Fi 页面不再使用长按上键切换语言。

新上传照片保留原始颜色。白色模式仅将显示缓存转换为灰度，不改动保存的照片。
以前上传时已经染绿的照片，在全彩模式仍会保留绿色；重新上传原图即可恢复颜色。
手机设置网页也可选择这三种外观。

### 1. 在手机打开设置网页

1. 在 Passport 首页按**确定**打开菜单，选择 **09 设置**，按**确定**，再打开 **Wi-Fi 网络**。
2. Passport 已连接家庭 Wi-Fi 时，让手机连接**同一个家庭网络**。
   Passport 已连接手机热点时，保持热点开启，使用提供热点的那部手机。
3. 在浏览器地址栏输入 **Passport 屏幕上显示的完整网址**，包括 `http://`。
   这是 Passport 的地址，不是路由器地址，也不是电脑地址。
4. 输入设备当前显示的**六位设备设置码**，然后点输入框外，让网页读取已保存资料。
   等待“已读取资料并同步设备时间”后再编辑；随后可在网页选择中文或英文。

| 手机与设备的连接方式 | 手机应打开的网址 |
| --- | --- |
| 手机连接 Passport 自己的 `Pocket-Passport` 配置 Wi-Fi | `http://192.168.4.1` |
| Passport 和手机都连接家庭 Wi-Fi | 设备显示的 `http://192.168.x.x` 地址 |
| Passport 连接手机热点 | 设备显示的地址，可能以 `172.` 或 `192.168.` 开头 |

**还没有联网？** 在 Passport 设置页**长按下键：配置 Wi-Fi**。
用手机打开 Android **设置 → Wi-Fi**，选择 **Pocket-Passport**，输入设备显示的
**密码 / Key**，再访问 `http://192.168.4.1`。如果 Android 提示“此网络无法访问互联网”，
请选择**仍然连接 / 保持连接**。这个 Wi-Fi 只用于本地设置，本身不提供互联网。

热点**密码 / Key**用于连接 Wi-Fi，六位**设置码 / Code**用于解锁设置网页，两者不同。
重启后两者可能变化，请以屏幕当前值为准。配置热点密码变化**不代表家庭 Wi-Fi 已被删除**。
切换网络后网页打不开时，重新查看设备设置页并使用新地址；请在网址栏输入，不要当作搜索词。

### 2. 连接家庭 Wi-Fi 或手机热点

使用 **2.4 GHz** 网络；ESP32-C3 无法连接仅提供 5 GHz 的网络。

| 在家 | 外出 |
| --- | --- |
| 保持家庭路由器开启。 | Android 开启**设置 → 热点与网络共享 → Wi-Fi 热点**，选择 2.4 GHz 或兼容模式；菜单名称因手机而异。 |
| 在 Passport 设置中用**上/下键：选择 Wi-Fi 网络**，选 **Home / 在家**。 | 在 Passport 上选择 **Phone Hotspot / 手机热点**。 |
| 按**确定：连接**，等待“已连接 Wi-Fi”。 | 按**确定：连接**，在线使用期间保持手机热点开启。 |

**在手机新增或修正网络信息：**在“网络与城市”选择已有私人预设或“手动输入网络”，
准确填写 Wi-Fi 名称与密码，包括大小写、空格和标点。点击**保存设置**，等待成功消息，
再在 Passport **长按确定：返回菜单**并重启，连接新网络。
已有预设也可以直接在设备设置页选择后按**确定：连接**。
仅退出设备设置或关闭网页不会保存尚未提交的表单。

重新打开网页时，密码框留空是正常的：**留空保留已有密码**，填写新值才会替换。
只有私人构建配置中提供的网络才会显示为 **Home / Phone Hotspot 预设**。
干净构建可以手动设置当前网络；要同时配置两个预设，请参考[从源码安装](#从源码安装)，
填写自己的信息。仓库不包含别人的家庭或热点密码。

如果手机连接 `Pocket-Passport` 时无法同时保持自身热点，请先在家庭 Wi-Fi 下配置，
或用第二部手机完成设置。Passport 连上手机热点后，可用提供热点的手机直接访问
设备分配到的地址，无需再次连接 `Pocket-Passport`。

### 3. 更换头像、资料和名片二维码

1. 打开手机设置网页，输入当前设置码并读取资料。
2. 在“个人名片”选择一张小于 **15 MB** 的照片，调整**缩放、水平位置、垂直位置**，
   直到预览合适。
3. 点击**保存新头像**，等待“新头像已保存到设备，重启后仍然保留”。
   这个按钮与**保存设置**是两个独立操作。
4. 返回 Passport 首页或名片页，新头像会自动显示，**无需刷机或重启**。
5. 修改显示名称、个性签名、联系方式或**名片二维码链接**后，点击**保存设置**。
   链接可使用 LinkedIn 个人主页、`https://wa.me/<国家代码和号码>` 或合适的微信分享链接。
   在设备名片页按**确定**切换二维码视图。

照片在浏览器内裁剪，并以 **80 × 112** 头像保存在 Passport 上，不上传照片云服务。
正常重启或使用电池不会删除照片与已保存设置；擦除固件或不兼容的升级可能删除资料，
请保留原始照片。上传新头像不会同时保存表单中其他尚未提交的修改。

### 4. 确认是否保存成功

成功消息出现在**手机网页**的“电脑桥接服务”区域中，位于**保存设置按钮正下方**，
并在网页底部再次显示。它不是 Passport 屏幕上的某一行文字。

| 修改内容 | 应按按钮 | 成功消息 / 后续操作 |
| --- | --- | --- |
| 头像 | **保存新头像** | 新头像已保存到设备，重启后仍然保留；返回首页或名片查看 |
| 姓名、二维码链接、语言、城市、PSI 区域、巴士站或桥接信息 | **保存设置** | **设置已保存。** |
| 网页中的 Wi-Fi 信息或所选预设 | **保存设置** | **设置已保存。** 然后重启，或在设备上选择已有预设并连接 |
| 用设备按键选择 Wi-Fi 预设 | **确定：连接** | 保存所选预设并尝试连接，等待“已连接 Wi-Fi” |
| 提供商账号授权 | 完成官方登录 | AI 用量账号区域显示**账号已连接** |

如果出现错误而不是成功消息，操作尚未确认完成，请保留网页、修正后重试。
已保存的 Wi-Fi 密码和桥接访问令牌不会重新显示在表单中。

### 5. 登录 ChatGPT / Codex 或 Claude 用量账号

先在电脑准备**桥接服务、Athena Usage Tracker**以及所需提供商的官方 CLI，
参阅[账号登录设置说明](companion/README.zh_CN.md#在设置时连接用量账号)。
用量看板通常使用 **4177** 端口，Passport 连接的桥接服务使用 **4188**；这是两个服务。

1. 保持电脑开机，并确保手机可以访问电脑。在家使用电脑局域网桥接地址，
   例如 `http://<电脑局域网IP>:4188`。这里**不要填写 `192.168.4.1`**，那是设备配置地址。
2. 在手机网页填写桥接的**服务地址**与**服务访问令牌**。需要设备使用此桥接时先保存。
   **保存后，在账号登录前重新输入令牌**；保存会清空网页中可见的令牌框，但设备已保存值仍保留。
3. 在 **AI 用量账号登录**选择 **ChatGPT / Codex** 或 **Claude Code**，填写账号名称，
   点击**添加用量账号**。已添加的账号可点击**读取账号**后选择，无需重复添加。
4. 点击**连接所选账号**。ChatGPT / Codex 会提供官方登录页面和一次性登录码，
   打开页面、输入该码并完成授权。Claude Code 必须**在桥接电脑上**完成官方浏览器登录，
   让其 localhost 回调能够返回电脑。
5. 等待**账号已连接**，然后在 Passport 打开 **AI 用量**并按**确定**刷新，
   用**上/下键**切换账号。未完成的登录可点击**取消本次登录**停止。

提供商的**一次性登录码**不同于 Passport 的**六位设备设置码**。
只在提供商的官方登录页面填写账号密码。提供商凭据保存在电脑私有 CLI 配置中，
不写入固件或 GitHub 仓库。

ChatGPT 登录用于查看 **Codex 剩余额度和重置时间**，不表示能读取单独的 ChatGPT
聊天消息配额或美元余额。Claude 用量还依赖看板的 Claude 状态行/同步数据，
仅登录成功仍可能显示“不可用”。可以只连接其中一个提供商。

### 6. 在家外使用电脑桥接

Android 开启**手机热点与 Tailscale**，Passport 连接手机热点。
手机连上 Tailscale **不会自动让热点客户端访问私有电脑**。

在手机设置网页填写电脑的 **Tailscale 服务地址**
（`http://<电脑Tailscale-IP>:4188`），重新输入**服务访问令牌**，点击**连接手机转发**。
保持网页在前台、手机不休眠。发送 Hermes 文字时勾选手机转发选项，并先在设备打开
**Hermes**。部分 Android 浏览器限制本地网络请求，连接失败时查看网页错误提示。

使用麦克风时，在设备打开 **Hermes**，按住**下键**，等待**正在录音**后说话，松开下键发送。
长按确定离开可取消尚未提交的录音。请保持手机网页开启直到回复到达，网页会转发麦克风音频和语音回复。
设备麦克风上传与取消已通过经过认证的转发客户端测试；用户已确认 Android 热点/Tailscale 转发的完整语音回复正常。不同手机和浏览器兼容性可能不同，请检查自己设备上的连接状态。

账号登录优先使用已填写的**电脑 Tailscale 服务地址**，留空时才使用普通服务地址。
如果要使用局域网地址，请清除过期的 Tailscale 地址。官方授权需要互联网，
不要在没有互联网的 `Pocket-Passport` 配置 Wi-Fi 上完成登录，请切回正常网络。

天气、NEA PSI 与巴士直接使用设备互联网连接。Hermes、AI 用量和广播需要电脑桥接运行；
手机转发还需要浏览器页面保持开启。Muse 使用单独的官方 Muse Passport 集成，目前仍在开发。
设置保存后可拔 USB，使用电池供电。

返回首页，或打开名片、天气、巴士、Muse，会结束浏览器手机转发。
下次使用时，请在设置中重新连接。Muse 使用独立的 Android 蓝牙伴侣连接。

### 7. 看懂状态消息与通知

这里的通知是**设备或网页上的状态消息**，不是手机推送、短信，也不是所有应用持续后台收信。
打开某项功能才会启动相应任务，离开后停止轮询、播放和尚未提交的录音；
已经提交的助手请求仍可能继续完成。Wi-Fi 保持可用，天气可以后台更新。
目前没有自动接收 Hermes/Muse 消息的推送信息流。

| 消息 / 指示 | 含义与处理方式 |
| --- | --- |
| **已读取资料并同步设备时间** | 设置码已验证、资料已加载，可以开始编辑 |
| **设置已保存。** / 头像已保存消息 | 对应保存操作成功；两个保存按钮相互独立 |
| **已连接 Wi-Fi** | 已加入 Wi-Fi，不代表电脑桥接一定可访问 |
| **正在连接…** | 正在尝试连接，等待成功或失败结果 |
| **Wi-Fi 失败 (…) / 确定重试** | 有限次数连接已结束；检查密码、名称与 2.4 GHz，再按确定重试 |
| **选择当前区域** / PSI `--` | 在手机选择 NEA 区域并保存；设备没有 GPS |
| **已过期 / 缓存 / 离线** | 数据可能较旧或来源不可达，检查互联网并刷新 |
| 巴士时间中的 **Arr** | 即将到站，其余数字单位为分钟；`--` 表示不可用，不是零分钟 |
| **内存不足 / 请稍后重试** | 运行内存不足，不是头像存储已满；停止广播或录音后重试，必要时重启 |
| **离线 / 请检查电脑服务** | 检查电脑、桥接进程、地址和令牌 |
| **手机转发已连接 / 已断开** | 浏览器转发启动或中断；需要时重连并保持页面唤醒 |
| **等待账号授权 / 账号已连接** | 继续官方授权 / 登录完成；用量是否可用还需另行检查 |
| **正在录音 / 松开下键发送** | Hermes 正在录音；松开发送，或长按确定离开并取消尚未提交的录音 |
| **音量 …%** | 当前广播音量，长按上/下键调整 |

PSI 设置位于**新加坡烟霾 → 当前位置区域**，选择当前新加坡区域后点击**保存设置**。
首页显示 NEA 发布的 **24 小时 PSI**，不是室内传感器实时读数。天气城市需要单独选择。
巴士最多可保存十个收藏站点，每行填写 `站点代码 | 名称`。
完整按键说明见[应用指南](docs/applications/pocket-passport.zh_CN.md)。

### 巴士数据来源与 API 访问

巴士到站现在**直接使用 LTA DataMall**，需要自己的 **Account Key**，
不会回退调用 Bus Aunty API。在手机设置网页读取资料后，填写 **LTA API 密钥**，
点击**保存设置**，再打开巴士到站并按**确定**刷新。以后编辑时留空保留已有密钥，
已保存密钥不会重新显示；它与桥接访问令牌不同。

通过 [LTA DataMall](https://datamall.lta.gov.sg/content/datamall/en/request-for-api.html) 申请自己的密钥。
数据来源为**新加坡陆路交通管理局（LTA）**，遵循
[Singapore Open Data Licence](https://data.gov.sg/open-data-licence) 与
[LTA API 条款](https://datamall.lta.gov.sg/content/datamall/en/api-terms-of-service.html)。
目的地显示为 **To stop …** 站点代码，界面更新时间为上次成功获取时间。
缓存和可选桥接端配置见[巴士设置详情](docs/applications/pocket-passport.zh_CN.md#巴士到站lta-datamall)。
此前的 Bus Aunty Mini 版权声明继续保留；本项目独立于两方运营者。

天气使用 Open-Meteo 无需密钥的免费接口，供个人/非商业用途使用。商业产品需要
合适的托管 API 套餐，参阅 [Open-Meteo 价格说明](https://open-meteo.com/en/pricing)。

## 🔒 凭据由你保管

私人 Wi-Fi 配置与设备/头像备份放在被 Git 忽略的 **`.local/`**。
干净克隆不包含个人网络凭据。私人构建会把配置嵌入固件，因此固件和 ELF
也必须留在本机。不要提交生成头文件、私人备份或原始串口日志。

## 🙏 来源、版权与许可

- **FoloToy AI Passport**：[完整上游](https://gitee.com/FoloToy/ai-passport)，
  基线 `a06235e4c94361e3f8a2d498930254101718acae`。
  **Copyright (c) 2026 FoloToy**，[MIT 许可](LICENSE)。
- **Bus Aunty Mini**：[awesomelionel/bus-aunty-mini](https://github.com/awesomelionel/bus-aunty-mini)，
  独立 ESP32-C3 实现的巴士功能/协议参考。
  **Copyright (c) 2026 awesomelionel**。已附完整
  [原始 MIT 声明和免责声明](assets/licenses/bus-aunty-mini-MIT.txt)，
  分发其代码的副本或实质部分时须保留该声明。
- **字体**：Droid Sans Fallback（Apache 2.0）与 DejaVu Sans Mono，
  详见[字体声明](assets/fonts/NOTICE.txt)和相应许可。
- **视觉参考**：[口袋百宝箱 438](https://ai-passport.folotoy.cn/plays/438/)。
  页面自行实现，未打包其闭源整机固件。
- **README 呈现参考**：[kokoro-onnx](https://github.com/thewh1teagle/kokoro-onnx)，
  借鉴友好的视频导览风格，未复制代码、品牌或媒体。

Bus Aunty 代码许可与托管 API、品牌、网站素材的使用权限是不同事项。
本项目是独立适配，不是 Bus Aunty 官方产品。

📚 [全部源仓库及用途](docs/applications/esp32superapp-build.zh_CN.md#项目来源)
· [FoloToy 原始文档](docs/README.zh_CN.md)

## 🧩 依赖与参考项目

`dependencies.lock` 固定固件版本，ESP-IDF 在构建时获取托管组件。实际依赖：

| 组件 | 上游仓库 |
| --- | --- |
| FoloToy 工程与 BSP | [FoloToy/ai-passport](https://gitee.com/FoloToy/ai-passport) · [FoloToy 官网](https://folotoy.com/) · [Passport 平台](https://ai-passport.folotoy.cn/) |
| ESP-IDF 5.5.3、网络与 JSON | [espressif/esp-idf](https://github.com/espressif/esp-idf) · [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON) |
| LVGL 9.5.0 | [lvgl/lvgl](https://github.com/lvgl/lvgl) |
| esp_lvgl_port 2.9.0 | [espressif/esp-bsp](https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port) |
| esp_codec_dev 1.6.2 | [espressif/esp-adf](https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev) |
| button 4.2.0 / cmake_utilities 1.1.1 | [espressif/esp-iot-solution](https://github.com/espressif/esp-iot-solution) |
| 电脑桥接 | [Python](https://github.com/python/cpython) 标准库 · 广播使用 [FFmpeg](https://github.com/FFmpeg/FFmpeg) |
| 简介渲染 | [HyperFrames](https://github.com/heygen-com/hyperframes) · [GSAP](https://github.com/greensock/GSAP)（独立许可）· [Inter](https://github.com/rsms/inter) |
| 中文/等宽字体 | [Android Droid](https://android.googlesource.com/platform/frameworks/base/) · [DejaVu](https://github.com/dejavu-fonts/dejavu-fonts) · 字体再生成使用 [lv_font_conv](https://github.com/lvgl/lv_font_conv) |

### 致谢社区项目与应用

| 项目 / 应用 | 在本项目中的作用 |
| --- | --- |
| [Folo AI Passport Voice](https://github.com/manchunx7-bit/folo-ai-passport-voice) | 语音功能参考与灵感 |
| [Tokens-Kanban](https://github.com/GreentailTohru-arch/Tokens-Kanban) | AI 用量看板参考 |
| [Athena Usage Tracker](https://github.com/luckeyfaraday/athena-usage-tracker) | 伴侣账号设置与配额适配使用的本地用量、账号注册接口 |
| [Muse Passport](https://github.com/FalkoWing/muse-passport) | 目标接入官方 Muse AI App 与 Android 伴侣；加密配对核心按 [Apache-2.0](main/muse/LICENSE) 移植，[移植说明](main/muse/README.zh_CN.md)；集成进行中 |
| [小智 AI 对话机器人](https://ai-passport.folotoy.cn/plays/xiaozhi-ai-chatbot/install/) | 官方社区应用与安装流程参考 |

感谢这些创作者开放分享。上述链接表示参考与致谢，不代表完整打包其应用或包含所有功能。
其他参考：[Bus Aunty Mini](https://github.com/awesomelionel/bus-aunty-mini)、
[AI Passport Muse](https://github.com/manchunx7-bit/ai-passport-muse)、
[AI Passport TV](https://github.com/zpdeng1020-coder/ai-passport-tv)。
Hermes 使用用户提供的兼容 API；Muse 指官方 Muse AI App 与 Muse Passport 伴侣。

## 🤝 贡献与支持

这是个人志愿维护项目，不承诺固定响应或支持时间。欢迎小修复、翻译、测试与硬件结果。
大型功能请先开 issue，提交复现步骤，日志须脱敏。

- [贡献与测试要求](.github/CONTRIBUTING.zh_CN.md)
- [行为准则](.github/CODE_OF_CONDUCT.zh_CN.md)
- [支持](.github/SUPPORT.zh_CN.md) · [提交 issue](https://github.com/benczb/esp32-uncle/issues)
- [安全报告](.github/SECURITY.zh_CN.md)，不要公开凭据或利用细节。

仓库源码采用所列许可，但在所有者决定更改前 GitHub 可见性仍为**私有**。
代码许可与仓库公开是两件事。项目按 [Open Source Guides](https://opensource.guide/best-practices/)
完善文档和贡献流程。

## 🚆 列车中断警报

在菜单中打开 **02 列车警报**，复用巴士到站已保存的 **LTA API 密钥**。
**上下键**翻页查看 LTA 提供的消息原文、受影响线路/站点编号、生效时间、原因/影响、
行程延误和取消信息；**确定**请求刷新，**长按确定**返回菜单。
原始消息保留来源语言，界面控件跟随中英文设置。

有警报时，首页 NEA 元数据栏替换为一行列车通知，PSI 和天气卡片仍然可见。
打开列车警报应用查看完整分页详情；没有报告警报时恢复 NEA 信息栏。

设备在首页或列车警报页，通过 Wi-Fi 直接获取 LTA 的 **GTFS Realtime Train Service Alerts**
和 **Train Trip Updates – Disruption** 两个数据集，自动刷新间隔至少两分钟。
每次获取新的签名下载链接；密钥仅发送给 DataMall，不发送给下载服务器。
无需额外密钥或电脑桥接，但 DataMall 账号必须有权读取这两个数据集。

**不可用、不完整和缓存不代表服务正常。** 请求失败时，已有警报标记为缓存，详情页显示错误。
成功获取空数据只表示 LTA 当前没有报告条目，并非列车服务正常的保证。
应用保留消息原文，不使用 AI 改写。下载与详情内存上限见
[应用指南](docs/applications/pocket-passport.zh_CN.md#列车警报)。

来源：[LTA DataMall API 指南第 2.32–2.33 节](https://datamall.lta.gov.sg/content/dam/datamall/datasets/LTA_DataMall_API_User_Guide.pdf)
和 [GTFS Realtime 规范](https://gtfs.org/documentation/realtime/reference/)。
