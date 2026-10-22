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

Settings is always the final directory item. Rendering and button navigation use
one shared order, with Settings appended after all feature apps.

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
| Flash | PASS — approved owner image written and hash-verified |
| USB startup diagnostics | PASS — three status replies across a 15-second observation, no panic/watchdog/assertion markers |
| Physical device and live phone/account behavior | NOT RUN |

The verified owner image SHA-256 is
`235bd4ab13ed3244777865e41205993c65cb421d1ec436d45d91c18f7404bd9e`.
Its matching ELF SHA-256 is
`eb465980c2551f9cd010f06b281905bcafaa3a23db69e1a127f290014dbcfc09`.
The image fits the existing application partition. After the owner granted serial
access, it was flashed at `0x0`; esptool verified the written data and reset the
device. The write ends at `0x29a30f` (erased sectors end at `0x29afff`), below
the identity reserve at `0x350000`. No full-chip erase was performed. A merged
flash can reset stored settings.

The device replied on Home, transitioned into its screensaver, and reported
approximately 60 KB free heap with a 51,200-byte largest free block. The captured
window contained one boot marker and no panic, assertion, watchdog, stack-canary
or brownout marker. Wi-Fi was not connected during this short observation.
Raw serial output is kept only in the ignored local device-test folder.

Pending physical checks: longer-term startup stability, the three directory entries and button
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

## Settings-last menu update

Settings is pinned after all feature apps by the shared menu-order function.
The complete gate, Muse-enabled owner build, firmware verification and bilingual
native menu rendering pass. This update is prepared but has not been flashed;
the device handoff above refers to the previously approved image.

New owner image SHA-256:
`13ce868772aa3206020304ca5e03d0a9a51b3a03d4e487cbf1ac7c49d9bc76cf`.
Matching ELF SHA-256:
`34815c19d3becf0deb852698fe2b1c821ccd41836b3d4cda7fe041252b71f0d8`.

## Phone setup and repository audit

The companion phone page now provides Komoot connection steps, a token check,
authenticated Spotify setup diagnostics, direct book selection, a current-page
preview and actionable errors. Selecting a book resumes its saved position and
is idempotent. Invalid IDs cannot become filesystem paths. EPUB conversion now
bounds cumulative chapter text before joining it, including repeated spine items.
The Spotify setup check rejects unsupported callbacks and prevents the phone UI
from starting a computer-only loopback login.

Readiness remains limited: the Komoot reference is deprecated and tested only on
iOS; current Android/iPhone app support needs a live test. Ebook page changes
require a reachable companion computer, and the phone relay does not forward
books or Spotify. This increment does not add offline storage or a phone app.
See [phone setup](../../companion/README.md#simple-phone-setup).

Build: PASS — complete gate, ESP-IDF 5.5.3 default configuration (Muse off).
Host tests: PASS — repository-wide gate and eight feature tests. Chromium with
a mobile viewport passed authentication failure/retry, setup diagnostics, TXT
upload, exact book selection, preview and credential clearing; layouts at
320/390/768/1280 px had no horizontal overflow or JavaScript errors.
Device tests: NOT RUN for this audit. Live Komoot, Spotify authorization and
physical reading/network coexistence remain unverified. No new firmware code
was changed and no device was flashed. Restart the companion to load the changes.

The gate's local verification archive is `build/firmware/ac4d79de44d2fe19a31ab277eec8076e892876669511cb28238b5c9a340b9c9f/`;
full image SHA-256 `ac4d79de44d2fe19a31ab277eec8076e892876669511cb28238b5c9a340b9c9f`,
matching ELF SHA-256 `b83e4a6cb584d6e1ba06e091462a02b58403f055f1515e3be2207852cccabd0b`.
This default test build does not replace the prepared Muse-enabled owner image.

## Android and iPhone setup

The shared `/tools` page now detects Android or iPhone/iPad and offers a manual
phone selector. It shows platform-specific Komoot permissions, browser guidance
and book file selection steps. Both use the same companion API and firmware.
This is a shared browser interface, not a new native app or iPhone relay.

Build: PASS — complete default firmware gate. Host tests: PASS — complete gate;
Chromium with Android emulation and WebKit with iPhone emulation passed platform
detection, manual switching, book upload/selection and 320/390/768 px layouts
without JavaScript errors. Device tests: NOT RUN. Actual phone file pickers,
Komoot BLE pairing, reconnect and background behavior remain unverified.
No firmware changes or flashing were needed. Restart the companion and reload
`/tools` to use the updated page.

Local gate archive: `build/firmware/bba66c84e6a360e99b768050091e04195695d6822eba230bb4fb9d645ddf4f25/`.
Full SHA-256: `bba66c84e6a360e99b768050091e04195695d6822eba230bb4fb9d645ddf4f25`.
ELF SHA-256: `109a04258eaac15ab5f53526f7d0b23c5a448eb2c322aad3968b2d8421b3d0f1`.
