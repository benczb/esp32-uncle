**English** · [简体中文](README.zh_CN.md)

# Pocket Passport companion

Run on the same LAN as the Passport. Python 3.10+ and `ffmpeg` are required;
there are no Python package dependencies. The computer must stay on for radio,
Athena and assistant requests. Public weather/PSI/bus data now work directly from firmware. Photos and profile data stay on-device.

```bash
python3 companion/start_local.py --bind <computer-LAN-IP> \
  --hermes-url http://127.0.0.1:8642/v1 \
  --hermes-env /path/to/hermes/.env
```

`start_local.py` creates a private `companion/.local/bridge-token` on first run.
Enter that token and `http://<computer-LAN-IP>:4188` on the device settings page.
The launcher imports only `API_SERVER_KEY` from the supplied Hermes `.env`, never
prints it, and does not change Hermes configuration. Its defaults expect a local
OpenAI-compatible Hermes API. Muse means the official Muse account and Muse Passport companion. The unrelated local-model route is disabled; BLE integration is pending.

Alternatively run `server.py` with `PASSPORT_BRIDGE_TOKEN` (16+ characters),
`HERMES_URL`, `HERMES_TOKEN`, `HERMES_MODEL`,
and `ATHENA_URL`. URLs for assistants end in `/v1`; Athena's URL is the root of
the existing port 4177 service. The bridge defaults to loopback. Bind to a trusted
LAN interface for a physical device; do not publish this HTTP service to the
Internet. The LAN hop is HTTP and carries its bearer token; use a trusted Wi-Fi.

## API

Data routes require `Authorization: Bearer <bridge-token>`. The static `/tools` page and state-validated Spotify callback are the exceptions.

| Route | Purpose |
| --- | --- |
| `GET /v1/status` | Time, station count, configured assistant flags |
| `GET /v1/athena` | Provider/status/quota only; no email or local credential paths |
| `GET /v1/weather?city=singapore` | Outdoor weather, 10-minute cache, explicit stale flag |
| `GET /v1/haze?city=singapore&region=central` | Latest NEA 24-hour PSI; manual region or `region=auto&lat=...&lon=...`, with location/stale/coverage states |
| `GET /v1/stations` | Six Singapore and four Malaysia stations |
| `GET /v1/radio?station=0` | 16 kHz signed little-endian 16-bit mono PCM |
| `POST /v1/chat/hermes` / `muse` | JSON `{ "text": "..." }`; asynchronous job admission |
| `GET /v1/reply/hermes` / `muse` | `idle`, `working`, `done`, or `error`, plus text |
| `POST /v1/voice/start` | Hermes capture admission: JSON `agent`, `sample_rate:16000`, `channels:1`, `format:"s16le"`; returns a random recording ID |
| `POST /v1/voice/chunk?session=...&seq=0` | Even-length PCM chunks, at most 4,096 bytes; sequential IDs, identical last-chunk retries are idempotent |
| `POST /v1/voice/finish` / `cancel` | JSON `session`; finish queues local transcription and Hermes, cancel discards unsent audio |

Radio decodes on the host and has at most two concurrent streams. Chat allows
one in-flight job per assistant. Device/settings requests are bounded, and
upstream error bodies are not exposed to clients. Replies are held in RAM and
are not durable chat history. The phone settings page supplies text messages;
Hermes supports microphone questions through the native computer bridge; replies are read aloud by the Passport speaker using the local eSpeak NG backend described below.

### Reply speech

Install `espeak-ng` and `ffmpeg` on the bridge computer (for Ubuntu:
`sudo apt install espeak-ng ffmpeg`). eSpeak NG runs locally, with no paid service
or API key. `PASSPORT_ESPEAK` can select an executable path. Chinese replies use
the Mandarin voice and other replies use US English. Speech text is passed over
stdin, never a command-line argument or shell. Synthesis and its one-reply cache
stay in RAM; no speech file is saved. eSpeak NG is a separately installed GPL
program; its sources are not part of this repository.

A completed Hermes reply includes `speech_id`. Authenticated
`GET /v1/speech/hermes?id=<speech_id>` returns 16 kHz mono signed 16-bit PCM for
that current reply; old IDs are refused. Repeated device polling does not replay
the same ID. Phone browser relay carries this audio using the existing bounded
PCM queue. On the Passport, OK opens volume; Up/Down changes it by 10%; OK closes
the panel. Hold Down records outside that panel; Hold OK leaves and stops audio.
Muse speech uses the official Android companion, not this endpoint.

### Local voice backend

On the Hermes page, hold Down until “Recording” appears, speak, then release Down to send. Holding OK to leave cancels an unsent capture. Native computer-bridge recording is limited to 30 seconds. Android microphone upload relay is pending. Muse voice uses its official connection, not this Hermes transcription route.

Install the optional dependencies in the bridge's Python environment with
`python3 -m pip install -r companion/requirements-voice.txt`. Prepare the model
once using `python3 -c "from faster_whisper import WhisperModel; WhisperModel('small.en', device='cpu', compute_type='int8')"`.
Runtime requests only load cached models and never download them. The default
`small.en` model recognises English. For multilingual speech, prepare a
multilingual model and set `PASSPORT_STT_MODEL` and `PASSPORT_STT_LANGUAGE=auto`
in the bridge environment before restarting it.

Audio stays in memory, is capped at thirty seconds of 16 kHz mono PCM and is
discarded on finish/cancel or the next request after a 35-second capture expiry.
Only one recording is admitted, and typed Hermes requests cannot interleave
with it. Finishing submits the recognised text to your configured Hermes;
cancelling cannot retract a message that has already been submitted.

Weather supports Singapore, Johor Bahru, Kuala Lumpur and Penang via Open-Meteo
(outdoor temperature, humidity, condition and wind; no physical weather sensor).
Radio presets are YES 933, LOVE 972, CAPITAL 958, CLASS 95, GOLD 905, 987, MY FM,
MELODY, HITZ and ERA. Public streams can change or become unavailable. The
station table is editable in `server.py` without reflashing the device.

## Checks

```bash
python3 tests/test_passport_companion.py
python3 tests/test_passport_haze.py
python3 tests/test_passport_settings.py
./tools/render_passport.sh  # after firmware dependencies have been fetched
```

The native renderer checks the actual generated LVGL font descriptors and both
themes with a 24 KB pool. It produces software-rendered PPM frames under
`build/screens/`; those are not photographs of a flashed device.

## Bus arrivals: LTA DataMall

Bus arrivals come directly from the official **LTA DataMall v3 Bus Arrival API**.
The device uses its own Internet connection, so the computer bridge is not needed
for this function. There is no fallback to Bus Aunty's hosted API.

1. Obtain your own [DataMall Account Key](https://datamall.lta.gov.sg/content/datamall/en/request-for-api.html).
2. Open the Passport phone settings page, enter the current six-digit device code
   and wait for your saved profile to load.
3. Enter the key in **LTA API key**, then click **Save settings**. Leave it blank
   on later edits to keep the saved key. The key is never returned to the browser.
4. On the homepage, press **Down** to open bus arrivals, then **OK** to refresh.
   A missing-key message asks you to finish step 3; a rejected-key message means
   LTA refused authorization. The API key is separate from the bridge access token.

The default favourite is **Eunos Int (82009)**. Save up to ten favourites in the
phone page as `stop code | name`, one per line; leading zeros are retained.
Four rows show service numbers and up to three arrival times in minutes. **Arr**
means arriving; **60+** means over an hour; **--** is unavailable. Destination
stop codes appear as **To stop 54009**, for example; this build does not fetch
destination names from LTA's separate Bus Stops dataset. Different destinations
and visit numbers remain separate.

Up/Down pages through services and favourites. OK refreshes; hold OK returns to
the menu. Requests are cached for sixty seconds; failures back off for
120/240/300 seconds. The displayed update time is our successful **fetch time**,
not an LTA source timestamp. Failed refreshes or data older than three minutes
are marked stale and their countdowns are hidden.

For the optional bridge bus route, set **`LTA_ACCOUNT_KEY`** in the bridge
process's private environment and restart the bridge. This is separate from the
key stored on the Passport. Do not put a real key in tracked files, command-line
arguments, firmware defaults or screenshots. `GET /v1/bus?stop=82009&page=0`
still requires normal bridge bearer authentication. A missing bridge-side key
returns `lta_key_required`; it never falls back to another operator's service.

Data source: **Land Transport Authority, Singapore**, via
[LTA DataMall](https://datamall.lta.gov.sg/), under the
[Singapore Open Data Licence](https://data.gov.sg/open-data-licence) and
[LTA API terms](https://datamall.lta.gov.sg/content/datamall/en/api-terms-of-service.html).
[API documentation](https://datamall.lta.gov.sg/content/dam/datamall/datasets/LTA_DataMall_API_User_Guide.pdf).
This project is independent and is not endorsed by LTA. Arrival estimates may change.
The existing [Bus Aunty Mini](https://github.com/awesomelionel/bus-aunty-mini)
reference and its MIT copyright notice remain credited for earlier work.

## Optional Android phone relay

To accept Hermes, usage and radio requests from the phone browser through Tailscale, bind
both explicit interfaces and enable the opt-in CORS policy:

```bash
python3 companion/start_local.py --bind <computer-LAN-IP> \
  --bind <computer-Tailscale-IP> --phone-relay \
  --hermes-env /path/to/hermes/.env
```

CORS accepts HTTP origins with literal RFC1918 private IPv4 addresses (the device
settings page). Bearer authentication is still required for all data routes;
OPTIONS only answers the preflight. No cookies are used. Do not use a public
interface or enable Internet port forwarding. Tailscale access rules must allow
the phone to reach TCP 4188. Android hotspot clients do not automatically share
the phone VPN; the browser performs bridge requests and forwards replies/PCM over the hotspot LAN.
Choose **Connect phone relay** on the device settings page for usage and radio;
Hermes messages use the relay checkbox. Keep the page in the foreground and
phone awake. Radio stops if the device misses eight seconds of heartbeats. Browser restrictions and
actual Android behavior still require device acceptance. Public bus/PSI/weather
now work directly from firmware without this bridge; radio and Athena still use it.

Athena defaults to `http://127.0.0.1:4177`. If the existing tracker listens on another interface, set `ATHENA_URL` to its actual root URL before launching (without `/api/usage`).

### One selected function

The Passport runs the selected function only. Leaving Radio stops its stream and releases codec/DMA and relay buffers. Leaving an assistant cancels an unsent microphone capture and stops its polling. Queued work from a previous function is discarded, while saved profile data stays available. The photo display buffer is loaded from saved flash by a worker on Home/Profile and released after leaving those pages; the saved photo is retained. Assistant display buffers are released on exit; reopening an assistant fetches the latest reply from the bridge again. Open the assistant page before sending a message from the phone settings page.

Wi-Fi and the settings web service remain available. Weather refreshes in the background about every two minutes; recording temporarily defers network work to fit internal RAM. Bluetooth is reserved as a shared connection service, but the official Muse BLE integration is still pending. A submitted request already running on the computer cannot be retracted by leaving its page; the computer's Hermes service is not stopped.

## Connect usage accounts during setup

Usage setup uses [Athena Usage Tracker](https://github.com/luckeyfaraday/athena-usage-tracker) on the bridge computer. Install its Node.js requirements, then start it locally on the bridge's default tracker port:

```bash
HOST=127.0.0.1 PORT=4177 npm start
```

Run that command in the tracker checkout. For a different address, set `ATHENA_URL` before starting this companion. Install the official [Codex CLI](https://learn.chatgpt.com/docs/auth) and [Claude Code](https://code.claude.com/docs/en/authentication) for the providers you want to connect.

1. Open the Passport's settings web page and enter the bridge URL and bridge access token.
2. Under **AI usage account sign-in**, choose **ChatGPT / Codex** or **Claude Code**, give the account a name, and click **Add usage account**.
3. Select the account and click **Connect selected account**. For OpenAI, open the official page and enter the displayed one-time code. Enable device-code login in ChatGPT security settings if required. For Claude, finish the official Claude Code browser login on the bridge computer; a phone cannot reach its localhost callback.
4. Wait for **Account connected**. Open AI Usage on the Passport and press OK to refresh. **Cancel this sign-in** stops an unfinished attempt; it does not log out an already connected account.

OpenAI sign-in connects a ChatGPT account for Codex quota windows and reset times. It does not imply access to a separate ChatGPT chat-message allowance. Claude authentication remains inside the unmodified Claude Code CLI; this app does not accept provider passwords or provider tokens. Claude quota values still depend on the tracker's native Claude status-line/sync data, so successful sign-in alone may leave limits unavailable.

Each setup account has a separate CLI profile under ignored `companion/.local/usage-accounts/<id>/`. The registry contains display metadata and a tracker account ID, not provider tokens. Native CLI credentials stay in that private profile; setup URLs/codes are kept only in memory and are not returned after completion/cancellation. The owner-only setup API uses the bridge bearer token, accepts no filesystem paths or shell commands, and supports up to six setup accounts. Existing tracker accounts are preserved.

Routes: `GET/POST /v1/accounts`, `GET /v1/accounts/<id>/status`, `POST /v1/accounts/<id>/login` and `/cancel`. Account creation registers the isolated profile with the configured tracker. Login uses the documented [Codex app-server device flow](https://learn.chatgpt.com/docs/app-server) or [Claude's own authentication flow](https://code.claude.com/docs/en/authentication).

If the bridge is reachable but AI Usage fails, check the usage tracker address separately. `ATHENA_URL` must match the tracker's actual listening address; its default is `http://127.0.0.1:4177`. The private launcher also reads an optional saved address from `companion/.local/usage-url` when `ATHENA_URL` is unset. An explicit environment setting takes precedence. The saved file is local and ignored by Git.

## Spotify and bookshelf

Open `/tools` on the companion to connect Spotify or upload UTF-8 TXT/DRM-free
EPUB. The page asks for the existing bridge token; it does not save it in browser
storage. Spotify needs `SPOTIFY_CLIENT_ID` and a registered callback (default
`http://127.0.0.1:4188/spotify/callback`), Premium and an active player. Optional
`SPOTIFY_REDIRECT_URI` changes the callback. Tokens stay on the companion.

Authenticated routes: `GET /v1/spotify`, `POST /v1/spotify/login`,
`POST /v1/spotify/control` (`previous`, `next`, `toggle`);
`GET /v1/library`, `POST /v1/library/import?name=book.epub` (raw file bytes),
`GET /v1/reader`, `POST /v1/reader/control` (`previous`, `next`, `open`, `size`,
`shelf`, or `select` with a bookshelf `id`). `GET /v1/setup` returns authenticated
Spotify setup flags without credentials. Control bodies are JSON objects with an `action` field. Only `/tools`
and the state-validated `/spotify/callback` bypass bearer authentication.

Limits, persistent data location, device controls and remaining live tests are
in [Navigation, Spotify and books](../docs/applications/pocket-passport.md#navigation-spotify-and-books).
The companion must remain reachable for Spotify controls and book page changes.
Komoot navigation is a direct phone-to-Passport BLE connection.

### Simple phone setup

1. Keep the companion computer on. Connect the phone and Passport to the same
   home Wi-Fi, with the companion listening on its LAN address.
2. Open **Spotify / Books setup** from Passport's phone settings. On `/tools`,
   enter the existing service token and tap **Connect companion**.
3. Choose a TXT/EPUB, tap **Add book**, then **Read on Passport** beside its title.
   Open **Books** on Passport. The selected book resumes its saved page.

The page checks authentication, explains connection failures and previews the
current page. Books and Spotify still require direct device-to-companion access;
the Android browser relay does not forward these routes. They are not standalone
phone-only services. Navigation uses direct Komoot BLE and needs no companion.

With Spotify's default loopback callback, complete sign-in once on the companion
computer, then control music from your phone/device. Bind the companion to
`127.0.0.1` as well as the LAN address for that callback (repeat `--bind`).
Phone sign-in requires a registered HTTPS callback that reaches the companion;
a plain HTTP LAN callback is rejected under
[Spotify's callback rules](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri).
The phone page disables sign-in when the callback requires the computer.
