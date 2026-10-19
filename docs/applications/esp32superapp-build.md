# ESP32 Uncle build notes

[简体中文](esp32superapp-build.zh_CN.md)

## Public source snapshot

This repository starts with one new initial commit containing the reviewed
current Pocket Passport application. Earlier repository history, conversation
logs, personal showcase media, private device configuration, backups and compiled
firmware are not included. Supply your own settings and accounts.

## Build and install

Follow [Start here](../../README.md#-start-here) and the
[ESP-IDF environment guide](../development/engineering/environment-setup.md).
Use ESP-IDF 5.5.3 and run `./tools/validate.sh` for repository checks, host tests,
build and merged-image verification. The target is ESP32-C3, 8 MB, without PSRAM.
The [build guide](../development/engineering/build-and-test.md) explains optional
Muse configuration and matching firmware/debug artifacts. Read the
[application guide](pocket-passport.md) for features and controls and the
[companion guide](../../companion/README.md) for services and account setup.

Flashing a merged image can reset saved device data. For an existing Passport,
follow the [stored-data policy](../development/engineering/firmware-layout.md#flashing-and-stored-data)
and use only compatible segmented images when retaining settings. This public
snapshot does not contain the maintainer's personal firmware or settings.

## Where the project came from

- [FoloToy AI Passport](https://github.com/FoloToy/ai-passport): board support, platform and development reference.
- [Muse Passport](https://github.com/FalkoWing/Muse-passport): official Muse Android companion integration and voice reference.
- [Folo AI Passport Voice](https://github.com/manchunx7-bit/folo-ai-passport-voice): voice interaction reference.
- [Tokens Kanban](https://github.com/GreentailTohru-arch/Tokens-Kanban): usage-tracking reference.
- [Bus Aunty Mini](https://github.com/awesomelionel/bus-aunty-mini): bus-display reference; production arrivals use LTA DataMall.

Retain the [root licence](../../LICENSE), Muse and Opus notices and
[asset notices](../../assets/README.md) when redistributing. Third-party
services require your own accounts/keys and remain governed by their terms.
