**English** · [简体中文](navigation-spotify-books-notes.zh_CN.md)

# Navigation, Spotify and books — implementation note

Date: 2026-10-22. This note accompanies the `feature/navigation-spotify-books`
branch. It describes the tested Pocket Passport increment before the newer
Traffic Light changes on `main` are integrated. Existing relay and screensaver
fixes in the working tree are retained and covered by the automated gate.

## What is implemented

- **Navigation:** Komoot phone directions over BLE, basic turn arrows, distance,
  street names, reconnect handling, stale-route status and a six-second overlay
  when the instruction changes. The phone supplies GPS and routing.
- **Spotify:** playback status, previous/next and play/pause through the companion.
  PKCE sign-in and token refresh run on the companion. Sound plays on the active
  Spotify player. Premium, developer-app access and a configured client ID are
  required.
- **Books:** companion upload of UTF-8 TXT and DRM-free EPUB, bookshelf selection,
  page turning, two Latin text sizes and per-book saved position. EPUB spine
  order is preserved. The device keeps only its current page; a reachable
  companion is required to change pages. CJK fallback remains 16 px.

Hold OK returns to the menu. The reader sleeps after 90 seconds of inactivity;
active navigation keeps its own screen awake. See the
[application guide](pocket-passport.md#navigation-spotify-and-books) and
[companion setup](../../companion/README.md#spotify-and-bookshelf) for controls,
limits and account setup.

## Validation and device handoff

| Check | Result |
| --- | --- |
| Complete repository gate, including host tests | PASS |
| Default ESP-IDF 5.5.3 firmware build | PASS |
| Muse-enabled owner build, merge/layout and archive verification | PASS |
| Native LVGL renders, both languages, reader sizes and overlays | PASS |
| Companion HTTP authentication, upload, reading persistence and PKCE tests | PASS |
| Companion JavaScript syntax | PASS |
| Interactive browser setup | NOT RUN — browser unavailable |
| Flash | BLOCKED — serial device detected but this session lacks permission |
| Physical device and live phone/account behavior | NOT RUN |

The verified owner image SHA-256 is
`235bd4ab13ed3244777865e41205993c65cb421d1ec436d45d91c18f7404bd9e`.
Its matching ELF SHA-256 is
`eb465980c2551f9cd010f06b281905bcafaa3a23db69e1a127f290014dbcfc09`.
The image fits the existing application partition. Flashing needs serial access;
no full-chip erase is required. A merged flash can reset stored settings.

Pending physical checks: stable startup, the three directory entries and button
controls, current Komoot app compatibility, real Spotify authorization/playback,
book page changes, and Wi-Fi/BLE/voice memory coexistence. Flash success alone
will not establish that these checks pass.

## Publication and privacy

Only source, tests and documentation are intended for GitHub. Private defaults,
Wi-Fi credentials, bridge/Spotify tokens, device identity/QR secrets, personal
portraits, uploaded books, raw device logs, firmware binaries and debug archives
remain outside the commit. The local owner firmware may embed private defaults,
so it is not included in the source publication. Tests use synthetic fixtures.
Runtime books and Spotify credentials stay in the companion's private data
folder; API responses never return Spotify credentials.

Protocol reference: [Komoot BLE project](https://github.com/jstiefel/esp32_komoot_ble).
The ESP-IDF/NimBLE receiver and its UI are implemented locally; no Arduino UI or
bitmap assets were copied. Other reviewed references were
[ESP32_GPS](https://github.com/aresta/ESP32_GPS),
[Ebook](https://github.com/CHENXiNNNX/Ebook),
[TinyLibrary](https://github.com/jkingsman/TinyLibrary), and
[AI Passport TV](https://github.com/zpdeng1020-coder/ai-passport-tv).
Full maps and TV are outside this increment.
