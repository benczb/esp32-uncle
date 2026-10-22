**English** · [简体中文](pocket-passport.zh_CN.md)

# Pocket Passport

A custom application on the official FoloToy AI Passport BSP. Branch:
`feature/athena-hermes-muse-companion`. Baseline upstream commit:
`a06235e4c94361e3f8a2d498930254101718acae`.

The visual reference is [Pocket Toolbox, play 438](https://ai-passport.folotoy.cn/plays/438/):
a profile portrait home page with selectable colour themes, PSI/weather alongside the portrait, a profile
card, and a separate application directory. This implementation has its own LVGL
screens and navigation; it does not use the baseline hardware-test menu or carry
over the reference app's closed whole-device firmware.

## Features and controls

| Screen | Controls and behavior |
| --- | --- |
| Home | NEA 24-hour PSI replaces the clock. Up: profile card; Down: bus arrivals; OK: app directory; hold Down: settings hotspot |
| Directory | Up/Down: select; OK: enter; hold OK: home |
| Profile card | Stored portrait, display name, tagline and contact text; OK cycles profile / QR code / NFC tap hint |
| Athena | Existing port 4177 quota data through the companion; Up/Down: account; OK: refresh |
| Hermes | Phone settings page submits text; Up/Down reads reply pages; hold Down to record and release to send through the configured computer bridge |
| Muse | Official Muse AI / Muse Passport integration is in progress; no working Bluetooth chat is claimed |
| Radio | Up/Down: station; OK: play/pause; hold Up/Down: volume with live percentage; hold OK stops and exits |
| Weather | Singapore/Malaysia city weather; OK: refresh |
| Settings | Shows IP and random per-boot setup code; Up/Down selects a supplied Wi-Fi preset, OK saves/connects; hold Down opens Wi-Fi setup; hold Up changes language |

Hold OK returns from any application to the directory. The radio worker owns
codec format, volume and PCM playback. Button callbacks only enqueue events.
Networking/audio run in workers; every UI access outside the LVGL task is locked.

## Change the profile picture

1. Open Settings. On first boot without Wi-Fi, the WPA2 setup hotspot is already
   available. Otherwise hold Down on Home/Settings to enable it.
2. Connect the phone to the hotspot shown on the screen, using its displayed
   password. Open the displayed HTTP address; enter the six-digit settings code.
3. Choose a photo, adjust zoom/horizontal/vertical crop, and save the new portrait.
   The browser converts the crop to 80 x 112 RGB565. No photo goes to the cloud.
4. Name, tagline, contact, city, theme and bridge configuration are saved separately.
   Empty password/token fields preserve their existing values. Wi-Fi changes take
   effect after restart; the new portrait and profile appear immediately.

The setup hotspot has no Internet access. Keep its connection when the phone
asks. Use 2.4 GHz Wi-Fi. Settings HTTP and bridge HTTP are for trusted local
networks, not Internet exposure. Saved passwords/tokens are not returned by the
settings API. The random settings code changes each boot.

## NFC tap-to-profile

The Passport contains a passive NTAG213 NFC tag. It is not connected to the
ESP32-C3: the firmware cannot write it, change it, or detect a tap, and no
BSP API exists for it. The Profile card's third view (press OK twice) only
explains the tap to the person holding a phone.

To use it, write the tag once from a phone:

1. Save your profile link in the phone settings page (the same **Profile QR
   link** the QR view shows; a LinkedIn URL fits easily in the tag's
   roughly 144 usable bytes).
2. In an NFC writer app such as NFC Tools, choose **Write → Add a record →
   URL/URI**, enter that same link, choose **Write**, and hold the phone
   against the NFC mark on the back of the Passport.
3. Test with a second phone if you can: hold it against the back and accept
   the banner or notification to open the link. Current iPhones and most
   Android phones read NDEF URL tags in the background with the screen on;
   no app is needed on the other person's phone, and the Passport itself
   can be switched off.

The tag holds one fixed link. To change it, write the tag again — or point
it at your own link page and change the destination there instead. Consider
password-protecting or locking the tag in the writer app after writing, so
someone else cannot overwrite it at a meetup.

## Screensaver and battery

On every screen, 10 seconds without a button press puts the display to
sleep: the backlight turns off and page fetching stops until the next press.
That press only wakes the display — it does not also navigate, toggle the
radio, or start a recording — and the current page then refreshes: bus
arrivals are fetched on wake (a still-fresh 60-second cache may answer
instead), and the weather and train timers fire as soon as they are due
rather than while the display is dark.

Wi-Fi is deliberately left associated in modem power-save while the display
sleeps. Disconnecting would trade a cheap associated idle for a full
reconnect, re-association, and TLS session on every wake, which costs more
power than it saves at this timeout; the saving comes from not fetching.
Radio playback continues with the screen off, including phone-relayed audio.
Only voice interaction and the Wi-Fi setup screen keep the display awake.
The relay connection stays active while the Passport sleeps; optional usage
and weather refreshes resume when it wakes. Keep the phone relay page in the
foreground and the phone awake.

The phone relay survives navigation through Home and other screens until
stopped or its heartbeat times out.

## Computer bridge and assistant scope

See the [companion instructions](../../companion/README.md). The bridge polls
Athena, fetches Open-Meteo weather, decodes the ten radio presets with ffmpeg,
and forwards assistant messages. On the development computer, Hermes is at
`http://127.0.0.1:8642/v1`, and the
bridge LAN address is `http://<computer-LAN-IP>:4188`. These are runtime settings,
not embedded credentials or required addresses in the firmware.

Muse means the official Muse AI account and [Muse Passport companion](https://github.com/FalkoWing/muse-passport). The integration uses the original companion protocol; the owner confirmed a paired spoken reply with sound.
Hermes supports phone text and hold-Down/release-to-send microphone input through
the configured computer speech bridge. The phone-browser relay also implements
microphone upload, cancellation and reply audio. The owner confirmed Hermes voice
on Home Wi-Fi and through the Android hotspot/Tailscale phone relay.
Live TV, Xiaozhi cloud voice,
games, PM2.5 and onboard GPS are outside this build. Phone-based Komoot
navigation is described below.
The [TV project](https://github.com/zpdeng1020-coder/ai-passport-tv) is a reference,
not an integrated TV feature.

## Storage and firmware

| Region | Offset / size | Purpose |
| --- | --- | --- |
| NVS | `0x9000 / 0x6000` | System data |
| PHY | `0xf000 / 0x1000` | RF data |
| Factory app | `0x10000 / 0x340000` | Application and compiled fonts |
| Identity reserve | `0x350000 / 0x10000` | Read-only reserved region, including `0x356000` |
| Profile NVS | `0x360000 / 0x20000` | Portrait and configuration |

The merged image ends before `0x350000`; do not pad it to the entire Flash size.
The profile partition is not preloaded into the merged image. The merge/layout
gate verifies image offsets, bounds and content. This layout does not imply
compatibility with another application's stored data. A merged flash can reset
system NVS and replaces the installed application. No full-chip erase is needed
or authorized. Flashing requires explicit permission for the exact artifact.

## Build and validation

```bash
export IDF_TOOLS_PATH="$PWD/.toolchains/tools"
source .toolchains/esp-idf-v5.5.3/export.sh
./tools/validate.sh
./tools/render_passport.sh
```

The repository gate includes navigation/UTF-8, bridge authorization/privacy/cache,
settings upload/RGB565/serialization tests and the original BSP/host tests. The
native LVGL renderer checks all 7,193 inventory entries, a missing-emoji negative
case, and screens in both languages and themes with the firmware's 24 KB LVGL pool.
Dynamic unsupported characters deliberately display `?`; arbitrary emoji and
all traditional Chinese characters are not promised.

For everyday setup, saving, account sign-in and status messages, use the
[README phone setup guide](../../README.md#-set-up-from-your-phone).

## References

- [Official full project](https://gitee.com/FoloToy/ai-passport)
- [Tokens Kanban](https://github.com/GreentailTohru-arch/Tokens-Kanban)
- [Voice module](https://github.com/manchunx7-bit/folo-ai-passport-voice)
- [Mediacorp radio](https://www.mediacorp.sg/corporate/brands/audio)
- [Astro Audio brands](https://astroaudio.com.my/our-brands)
- [Open-Meteo weather API](https://open-meteo.com/en/docs)

## NEA haze indicator

The home screen replaces the clock with the latest **24-hour PSI**, its NEA
category, reporting region, and reading time in Singapore time. It does not
present PSI as an instantaneous particulate measurement. Values above 100 use a
contrasting warning color in both themes. Missing location, failed requests and
stale readings have explicit states; no low/healthy value is invented.

Select Central/North/South/East/West in phone settings, or choose coordinate mode.
Coordinate mode maps the saved phone coordinates to the nearest official NEA
reporting-region label centre, not to an authoritative regional-boundary polygon.
There is no onboard GPS or continuous location tracking. A conservative Singapore
coverage rectangle rejects distant coordinates; the selected weather city must
also be Singapore. Other cities show that NEA coverage is Singapore-only.

The phone location button requires HTTPS or localhost. Normal device HTTP pages
cannot obtain browser geolocation; choose the region manually or enter coordinates
there. The browser explains this limitation. Coordinates are saved locally and
sent only to the local bridge; the NEA request fetches all regional readings and
contains no personal coordinates. Haze settings use a separate NVS key, preserving
the existing profile/configuration blob format.

The bridge caches valid NEA data for five minutes, retains valid cached data on
upstream failures, and flags failed-refresh or more-than-two-hour-old readings as
stale. The device shows the source reading time, not the time it fetched data.
An unconfigured device asks for a location rather than assuming Central Singapore.

Sources: [NEA PSI API](https://api-open.data.gov.sg/v2/real-time/api/psi),
[NEA PSI definitions](https://www.nea.gov.sg/our-services/pollution-control/air-pollution/faqs).
Run `python3 tests/test_passport_haze.py` for boundary, region, location, cache,
malformed-data and stale-reading checks. Software previews use example data;
physical location changes, saved settings and screen output still need device
acceptance.

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

## Android hotspot and outdoor use

Bus arrivals, NEA PSI and Open-Meteo weather now use the Passport's own HTTPS
connection through a 2.4 GHz phone hotspot. NTP must synchronize time before TLS
certificate checks can succeed. An initial connection can take a little time.
Weather caches for ten minutes, PSI for five minutes and bus data for one minute;
failed refreshes are marked stale. PSI location is saved, not continuously tracked:
update the region/coordinates after moving. Malaysia has no NEA PSI coverage.

Android Tailscale does not automatically route hotspot clients through its VPN.
For Hermes, open the device's settings page **on the phone**, check
the phone Tailscale relay option, enter the computer's Tailscale bridge URL and its
access token, and send a message. Keep the page open until the reply reaches the
device. The token must be entered for this page session; saved secrets are never
read back. The phone browser must be included in Tailscale's VPN routing.
Browser local-network/CORS restrictions can prevent this flow; real Android
acceptance remains pending. A relay error is shown on the phone and device.

The bridge must listen on its Tailscale address with `--phone-relay`; see the
companion instructions. It remains private to the LAN/tailnet. The home computer
must stay awake. For AI Usage and radio, choose **Connect phone relay** on the
phone page after entering the bridge URL and token. Keep the page in the foreground
and the phone awake. Usage refreshes every 30 seconds; radio follows the Passport
station/play/volume buttons. PCM is forwarded in bounded 1,024-byte packets with
backpressure and playback-generation checks. An eight-second missed heartbeat
stops device playback. **Stop relay** restores direct bridge mode. Muse remains
a separate official Bluetooth integration, not an HTTP/local-model assistant.

The application directory wraps: Up from item 1 selects item 8, and Down from item 8 selects item 1.

## Private Wi-Fi profiles and backups

An ignored `.local/passport-defaults.json` can hold WPA2 profiles with IDs `home`
and `outside`. The generator writes a private header only inside the build tree.
A normal clean clone contains no credentials. Never commit the configuration,
generated header, credential-bearing firmware or ELF. A changed local profile
set applies its chosen default once; later UI edits remain respected, and the
profile card/photo and other settings are retained.

The same private JSON may include `bridge_url` and `bridge_token`. These fill
missing device bridge settings only; a token is seeded only when the saved URL
matches the configured private URL. Existing nonempty settings remain intact.
Optional `bridge_lan_url` selects the computer LAN endpoint for Home when the
saved service URL still matches `bridge_url`; custom saved URLs take precedence.
The stored Tailscale URL remains available to the phone page. Changing these
fields does not reapply the Wi-Fi default. This configures bridge
authentication, while device-to-computer network reachability is still required.

For local USB acceptance testing, `usb_test_control: true` enables exact
`@passport status`, `@passport up/down/ok`, and `@passport hold-up/hold-down/hold-ok`
lines on the USB console. They use the same input queue as the physical buttons.
Status reports counts, connectivity and heap measurements, never credentials,
profile text or assistant replies. Input is bounded and nonblocking. The flag
defaults to false and is disabled in ordinary clean builds.

Private USB diagnostics also accept `@passport wifi-awake` (b/g/n with modem
sleep disabled), `@passport wifi-legacy` (b/g only, modem sleep disabled), and
`@passport wifi-normal` (restore b/g/n and modem sleep). Use `@passport wifi-baseline` to test modem sleep during connection. These do not
change saved credentials and are temporary until reboot; they are not automatic
protocol fallbacks. `@passport setup` prints the current device IP and setup code
for a local USB web/API test; treat that output as a secret and keep it out of Git.
TLS retains 16KB incoming-record support and full CA verification, with dynamic
buffers and 1KB outgoing records to leave SRAM for certificate verification.

In device Settings, Up/Down selects Home or Phone Hotspot; a short OK saves and
connects immediately. Wi-Fi and language commands use a separate priority queue
so background polling cannot silently discard them. Normal modem power saving
remains enabled outside private compatibility trials. Hold Down still opens the phone setup hotspot; hold OK
returns to apps. The phone settings page also has a profile selector and now
shows save results beside the Save settings button. Phone-saved Wi-Fi changes
still take effect after restarting; saved passwords are not returned.

User-requested private backups live in `.local/backups/`: raw profile partition,
decoded configuration and an exported device 80 x 112 portrait. They contain
passwords/tokens and stay local, outside GitHub. The portrait is the device image,
not the phone's original full-resolution photo; RAM-only assistant replies are
not included. Restore only against a compatible partition layout; a profile
backup is not a whole firmware image for flashing at 0x0.


## QR, language and connection diagnostics update

On Profile card, short OK toggles profile / QR. Phone settings accepts an HTTPS,
HTTP or WeChat share URL. Personal default links stay in ignored private build
input. Hold Up in Settings to change Chinese / English, or choose Language on
the phone and save. Both settings use separate NVS keys without changing the
existing stored profile struct.

After entering the phone setup code, the page uses authenticated `/time` to
synchronise phone time before loading the profile. Firmware also tries three
NTP servers and retains certificate verification. Weather and bus errors now
identify clock, connection, HTTP or response failures. Wi-Fi stops after three
disconnections or its connection deadline; the numeric reason is retained and
OK retries.

AI Usage replaces the Athena screen name. The bridge retains `/v1/athena` and
adds `/v1/usage`, returning only allowlisted quota fields. Codex / Claude show
remaining percentage, window duration and reset countdown. Missing, invalid and
unconnected data never become a full allowance. The live development tracker
currently supplies three Codex accounts and no Claude quota.

Device evidence: user confirmed phone hotspot connection, retained photo and
menu wrapping. Home Wi-Fi fails. Fresh weather/bus/radio remain unverified.
The new QR, language persistence and clock fallback await this firmware's device
test. Muse means the official Muse app + Muse Passport Android companion, which
the user has installed and linked. Its encrypted BLE protocol integration is
still pending in this superapp; the incorrect local-model route is disabled.


### Preset password, Settings layout and request memory fixes

After choosing Home/Phone Hotspot on the phone, enter a new password to correct that
preset, or leave it blank to retain its saved password. Separate private NVS
keys retain each override across reboot and device profile switching. Passwords
are never returned, and this does not change private build input in Git.
Hotspot name, maximum-length password and setup code fit a bounded status area;
the address uses a smaller Latin font. Language changes explicitly repaint an
opaque background. Public requests wait for a Wi-Fi IP address; response buffers
start at 1 KB and grow as needed within the original limits, retaining HTTPS
verification.

Private build input may include `psi_region` (`central`, `north`, `south`, `east` or `west`). On first provisioning it fills an unselected region and saves it to NVS, without replacing an existing selection. A saved seed prevents reapplying it after the user clears the region. Public builds leave the region unselected.

The radio worker fully releases BSP codec/I2S/DMA resources and the relay PCM queue when playback stops, then recreates them for the next playback. Queue creation, nonblocking reads/writes and deletion share the application mutex, so no HTTP handler can enqueue into a deleted queue. This returns RAM to certificate-verified public-data requests after leaving radio; the shared I2C bus remains available to the display and battery gauge.


## Train alerts

Menu item **02 Train alerts** uses the same saved DataMall AccountKey as bus
arrivals. Settings remains item 09. The nine-item menu scrolls to keep the
selection visible and wraps at both ends. Homepage Down still opens bus arrivals.
An active train notice occupies the existing NEA metadata strip; it does not
cover the photo, PSI or weather. No sound or vibration is generated.

Home and Train alerts request both GTFS datasets at most once per two minutes;
entering Train alerts requests its detail snapshot, and OK requests a refresh.
Wi-Fi and a synchronized clock are required. The network owner serializes HTTPS
with weather and pauses new work during recording. Other foreground functions
do not poll train data. Full detail buffers are released when leaving Train
alerts; the homepage retains only the headline, count and freshness state.

The service-alert feed's communication periods (or legacy active periods) are
interpreted as half-open intervals; expired or future-only entries do not
produce a current alert. The app displays all supplied language variants of
headlines/descriptions, URLs, affected identifiers, periods, cause/effect codes
with common names, and disruption trip/stop predictions, delays and cancellations.
GTFS route and stop identifiers remain exactly as supplied; no station-name
lookup or invented incident description is used. Feed timestamps may legitimately
be old for ad-hoc data; retrieval failure, not age alone, marks cached data.

For ESP32-C3 memory safety each protobuf download is limited to 64 KiB and
combined paged detail text to 32 KiB. Oversized, malformed, differential or
failed feeds show an error/partial state instead of silently truncating details
or claiming normal service. Only successful snapshots from both feeds produce
the no-reported-alerts state. Partial results may contain useful details, but
remain labelled partial/cached. A previous known warning is retained on failure.
The homepage intentionally shortens its one-line headline; open the app to read
the source text over font-measured pages.

AccountKey is attached only to the fixed HTTPS DataMall metadata endpoints.
Signed downloads use a new handle with no key, no bearer token and no automatic
redirect following. Download URLs must match the documented LTA S3 host and
train-data paths; signed links and keys are never logged or stored in Git.

## Navigation, Spotify and books

The directory now also contains Navigation, Spotify and Books. These are part
of Pocket Passport's own interface. All three preserve hold-OK to return to the
menu; slow operations stay on workers rather than button callbacks.

| App | Controls | Data source |
| --- | --- | --- |
| Navigation | OK starts/stops; hold OK leaves the screen while navigation continues | Komoot phone app over BLE |
| Spotify | Up previous track; Down next track; OK play/pause | Authenticated computer companion |
| Books | Up/Down choose books or turn pages; OK opens/returns to bookshelf; hold Up changes text size; hold Down opens bookshelf | TXT/EPUB converted and paginated by companion |

### Komoot

Open Navigation, press OK, and start a route in Komoot on the phone. The device
scans for Komoot service `71c1e128-d92f-4fa8-a2b2-0f171db3436c`, discovers the
navigation characteristic and notification descriptor, subscribes, and reads
complete instructions at least every three seconds. It supports long GATT reads,
bounded UTF-8 street names, distance, maneuver labels, arrows for basic turns,
arrival and off-route states. Unsupported maneuvers retain their text label.
The phone supplies GPS and routing; no extra GPS hardware is needed.

Navigation continues after leaving its screen. A changed instruction shows a
six-second overlay on other screens and wakes the display. Returning to
Navigation and pressing OK stops the link. Its screen stays awake while enabled;
other screens retain their normal timeout. Readings older than ten seconds are
marked stale and do not produce a turn overlay. Disconnections trigger scanning
and reconnection. A stopped route is shown as waiting, never as a fabricated
arrival. Current phone/Komoot compatibility remains an on-device acceptance test.

The default build enables NimBLE central/observer roles and
`CONFIG_PASSPORT_NAVIGATION_BLE`. Startup is lazy to conserve heap. If optional
Muse BLE is enabled, navigation shares its host instead of initializing another
Bluetooth stack; that variant also needs central/observer roles and at least two
connections. Wi-Fi, voice audio and simultaneous BLE links require board tests.

### Spotify setup

1. Configure a Spotify developer application with the exact redirect URI
   `http://127.0.0.1:4188/spotify/callback` (or an HTTPS URI routed to this bridge).
2. Set `SPOTIFY_CLIENT_ID` in the companion's launch environment. Optionally set
   `SPOTIFY_REDIRECT_URI` to match the registered URI. Restart the companion with
   those variables; no Spotify client secret is needed for PKCE.
3. Open `http://127.0.0.1:4188/tools` on the companion computer, enter its existing
   bridge token, then choose Connect Spotify. Complete the Spotify login and
   return to `/tools`. A loopback callback must be opened on that same computer;
   phone login needs a reachable HTTPS callback and a matching registered URI.
4. Start playback in Spotify on your phone or another Spotify player, then open
   Spotify on the Passport. Premium, developer-app access and granted playback
   scopes are required. This is a remote; music stays on the Spotify player.

The companion refreshes tokens privately and caches playback state for five
seconds. It honors rate-limit backoff and returns connection/setup failures.
OAuth state expires after ten minutes and is single-use. Tokens are never sent
to the Passport or returned in status responses.

### Book import and persistence

Open the companion's `/tools` page, enter the bridge token and upload UTF-8 TXT
or DRM-free EPUB (up to 4 MB, 32 books). EPUB conversion follows spine order,
removes markup/scripts and never extracts archive paths to disk. Expanded
archives are capped at 16 MB and 2,048 entries. PDF, DRM and embedded book images
are not supported. Select the book on the Passport and press OK to read.

The companion stores books, selected book, text size and per-book reading
position under `~/.local/share/pocket-passport`, or `PASSPORT_DATA_DIR` when set.
It also stores Spotify tokens there with private permissions. Back up this
folder to retain your library and progress. Changing text size approximately
preserves the reading location. One companion reading session serves one
Passport. The device retains only the current text page in RAM; a full offline
library is not implemented. A lost companion connection labels the retained
page offline. Books use a 90-second idle timeout instead of ten seconds.
Arbitrary characters remain subject to the existing font coverage contract;
unsupported characters display `?`. The larger reading mode uses a 20 px Latin
font with the existing 16 px CJK fallback; it does not enlarge CJK glyphs.

Implementation acceptance: host protocol, HTTP authentication, PKCE state,
EPUB ordering and persistence tests plus native LVGL renders. Live Komoot,
Spotify account authorization and physical button/display behavior require the
owner's phone and device. No account login or firmware flashing is automatic.

### Implementation checkpoint — 2026-10-22

Accepted scope: phone Komoot navigation, Spotify remote controls, and a companion
TXT/EPUB bookshelf with saved position. All three are implemented. The complete
repository gate and native LVGL layouts pass; the Muse-enabled owner variant
also builds and passes merge/layout and archive verification. The setup page has
HTTP tests and JavaScript syntax checks; browser interaction was unavailable.

Owner delivery image SHA-256:
`235bd4ab13ed3244777865e41205993c65cb421d1ec436d45d91c18f7404bd9e`.
Matching ELF SHA-256:
`eb465980c2551f9cd010f06b281905bcafaa3a23db69e1a127f290014dbcfc09`.
The verified archive is `build/firmware/235bd4ab13ed3244777865e41205993c65cb421d1ec436d45d91c18f7404bd9e/`;
`build/FoloToy-AI-Passport-full.bin` is the same owner image. The default gate
archive remains separate and has optional Muse disabled. Continue the owner
configuration with `idf.py -B build/all-three -D SDKCONFIG=build/all-three/sdkconfig build`;
it enables Muse, navigation, central/observer roles and two BLE connections.

Device tests: NOT RUN; no compatible serial port was visible. Pending: current
Komoot phone pairing and maneuvers, Spotify login and real player controls,
book import/page turning on the device, and Wi-Fi/BLE/voice memory coexistence.
No flash, commit, push or account authorization was performed.

[Feature delivery note](navigation-spotify-books-notes.md)
