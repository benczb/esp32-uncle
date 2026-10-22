**English** · [简体中文](README.zh_CN.md)

# Passport screen gallery

The [gallery](index.html) keeps the existing cover first, then shows 24 views in
each language. Swipe left, drag or use the buttons/keyboard to reveal the next
image. Select an image to enlarge it. English and Chinese use separate exports.
The root README includes a GitHub-compatible horizontal image table; the linked
[public gallery](https://benczb.github.io/esp32-uncle/) provides the full carousel.

## Source and reproduction

- `images/`: 48 final 1080 × 1440 PNG stills exported by HyperFrames 0.8.185.
- `screens.json`: view names, language, snapshot times and final filenames.
- `hyperframes/`: editable composition, with one sub-composition per still.
- `hyperframes/assets/screens/`: frozen 240 × 320 LVGL renders with synthetic
  data, made from the production renderer at commit `3d74cb7`. The traffic lenses
  come from the public `main` artwork at commit `43a0856`; those two views are
  illustrations of the traffic interface, not firmware acceptance evidence.
- `hyperframes/assets/home-portrait.jpg`: the owner-supplied illustrated portrait,
  explicitly authorized for Home and Profile after the initial privacy exclusions. It is
  positioned in the Home and Profile avatar slots by the composition. The original has no
  EXIF metadata. Other personal photos remain excluded.
- The first cover is the existing `../images/pocket-uncle-cover.png` from `main`.
- GSAP 3.14.2 is bundled solely for the paused snapshot timeline. Its original
  header is retained; see [its notice](hyperframes/assets/vendor/NOTICE.txt) and
  [standard license](https://gsap.com/standard-license/). Project-authored source
  follows the repository license; third-party licenses remain applicable.

Run from `hyperframes/` with Node.js 22+ and a working Chrome:

```bash
npm run check
npm run snapshots
# Re-export just the authorized Home portrait views:
node export.mjs --home
```

No video, audio, cloud renderer or private runtime service is required. The
images are software previews rather than photographs or live service readings.
The gallery spans the `main` traffic app and the navigation/music/books feature
branch; it does not imply those firmware branches have been merged.

## Public content boundary

The only QR payload is the explicitly authorized public profile:
`https://www.linkedin.com/in/ben-cheng`. Both final QR exports were decoded and
matched to that exact URL. There are no phone numbers, WeChat/WhatsApp QRs,
gadget identifiers, AI provider/account details, private tokens or live profiles.
Home and Profile contain the authorized illustrated portrait. Account views
use generic demo content. No device or account was queried.

The publication consists only of these curated images, static gallery source
and the existing cover. Firmware, build trees and private configuration are not
part of the gallery or its GitHub Pages deployment.

## Publication checks

The GitHub Pages homepage returns HTTP 200. All 48 deployed PNGs were fetched
and matched byte for byte to the reviewed local exports. A scan of every final
image found only the two authorized LinkedIn QRs, both with the exact approved
URL. HyperFrames passed with zero lint/layout/contrast findings. Android Chrome,
iPhone WebKit and desktop checks covered the cover-first order, next/previous,
left drag, all 25 slides per language, full-size viewing, keyboard controls and
language switching. Repository checks pass. This is a media/documentation update;
no firmware was changed or flashed.

A persistent scrollbar below the images supports phone and desktop browsing.

Gallery order: cover, Home, Menu, demo Codex balance, Profile card, LinkedIn QR, NFC profile, then the remaining screens. Codex values are synthetic examples, never account readings.
