[简体中文](README.zh_CN.md) · **English**

# Official Muse pairing core

These non-UI files are adapted from [FalkoWing/muse-passport](https://github.com/FalkoWing/muse-passport), commit `664f5c2a3c9fb9c0b0bf4ed9801907c7f01fdb3e`, under the included Apache-2.0 [license](LICENSE). Original copyright notices are retained.

The port keeps the upstream encrypted provisioning protocol and device identity format. Adaptations remove manufacturing-test commands, isolate settings in the existing `profile` partition under the `muse` namespace, and avoid an embedded SDK token. The ESP32-C3 port uses upstream community v5 authentication, without manufacturer attestation, and the public Mbed TLS GCM header supplied by ESP-IDF 5.5. It never writes eFuse. It does not copy upstream screens, fonts, avatars, or board drivers.

This is an integration in progress. The production firmware does not enable it yet. Android relay acceptance, resource coexistence and chat/voice still require integration and on-device verification.

The upstream-adapted tests in `tests/muse_pairing/` retain their copyright notices and use this same license. They exercise shared v5 transcript vectors, storage failure/isolation, signer policy, and the actual community handshake with real host cryptography (confirmation, replay, expiry and session binding). A separate build-only C3 probe links the BLE pairing core; it is not a deployable superapp image.

The extracted Android transport retains upstream authenticated GATT framing, token rotation, bounded requests and IMA-ADPCM microphone upload. Assistant reply-speech negotiates the original `opus-16000-60-v1` protocol; the bounded upstream speech window and decoder are retained, with BSP output and shared-volume adapters. Its redraw hook is supplied by the optional superapp adapter; the core does not access LVGL. The build-only probe provides no user interface and must never be flashed to the Passport. The portable encoder is checked against the official Android `AudioUpload` implementation, including sample duration and missing-block rejection.

Relay startup now returns a retryable failure instead of aborting on low memory. Partial queues, semaphores and tasks are released before returning; initialization must run serially before registering BLE callbacks. Host fault injection covers every startup allocation/task failure and a successful retry. This does not establish runtime pairing or Bluetooth acceptance.

BLE server startup also checks the NimBLE return value, all application service registrations, device-name setup and host-task creation. It owns the host task because the ESP-IDF 5.5 FreeRTOS convenience adapter discards task-creation failures. A failed startup releases its mutexes and deinitializes the initialized stack before retry; if stack rollback itself fails, further startup is refused until reboot. This does not verify SDK-internal failure handling, RF behavior or Android pairing.

`setup_controller` adapts the official callbacks for device information, encrypted physical confirmation, Wi-Fi scan/join hooks and account provisioning. The button queues a captured prompt generation; a worker confirms that exact session under the pairing lock. All credential writes and the completion marker share a session-bound commit; stale Wi-Fi/token results cannot write credentials. Optional legacy token minting is an application hook, with temporary tokens wiped before free. Modern device-token pairs are accepted directly. OTA and remote unpair callbacks are deliberately unwired in this controller. The optional application adapter described below supplies current-network hooks and confirmation/passkey UI; the voice transport is implemented separately below, with audible Android acceptance still required. The build-only probe uses a failing Wi-Fi hook and remains unsuitable for flashing.

Pairing initialization returns a checked result before BLE startup. Mutex allocation and P-256 curve-load failures refuse setup and support retry; real-crypto host tests inject both failures before a successful handshake.

The optional application adapter `main/passport_muse.c` now starts pairing on the
existing network worker when Muse is opened, queues physical OK confirmation,
and displays Android Bluetooth passkeys in the independent superapp UI. It
waits for inactive portrait RAM to be released, avoids the unused chat buffer and
legacy Muse HTTP polling, and leaves the Bluetooth service running after exit.
The phone chooses the Passport's already-connected Wi-Fi; change networks in
superapp Settings first. This adapter does not provision a second Wi-Fi manager.

`CONFIG_PASSPORT_MUSE_BLE` remains off in the normal build. An experimental build
requires NimBLE, persistent bonds and Secure Connections. Host tests exercise
startup failures/retry, advertising, current-network reuse, confirmation,
passkeys, bond recovery and deferred photo release. Layout tests cover English
and Chinese pairing panels in both themes. The Bluetooth-enabled full superapp
compiles, but on-device RAM coexistence, actual Android pairing and chat/voice
are still unverified. Do not present this experimental pairing adapter as a
completed Muse assistant.

The experimental voice path now connects hold-Down/release-to-send to the BSP
microphone and the official Android IMA-ADPCM upload. Audio remains on the
existing audio worker; the network worker pauses new requests during recording.
The relay receives a bounded 16 kHz mono recording, reads the history cursor
before submitting its final frame, and correlates the acknowledged note ID with
ready assistant replies. Leaving Muse cancels open requests and frees response
buffers; stale callback generations cannot update a replacement turn.

Actual firmware-source host tests cover the microphone-to-reply path, early
release, unsent cancellation, codec failure, inactive-page cleanup, fragmented
response frames, allocation/send errors, oversized/malformed replies, timeouts,
and 1,000 cancelled/replaced turns. This establishes local state-machine behavior,
not live Muse service success. The optional BLE build still requires real Android
pairing, microphone/relay acceptance and SRAM/stack coexistence checks before
being enabled by default. Typed Muse input through the phone settings page and
assistant speech playback are not implemented.

The on-device reply buffer retains at most 6,399 UTF-8 bytes; use the Muse phone
app for a longer response. After a completed turn releases its history buffers and microphone workers, the companion supplies Opus reply speech. The decoder/task exist only during playback. Raw public-data caches are released while displayed weather remains visible; networking resumes after playback. The separate upstream checkout is unchanged.

The superapp allocates a 6 KB `phone_audio` task stack rather than the upstream
4 KB. A paired-device recording triggered a stack-protection restart on the
4 KB worker. This integration-only adjustment does not change the BLE protocol,
SDK-token persistence or the separate upstream checkout.

During Muse recording the superapp releases the previous 6,400-byte reply
buffer and uses a fixed 320-byte progress/status buffer. It allocates the reply
buffer again only when assistant text arrives, after microphone DMA is released.
This leaves additional heap available to the Bluetooth controller without
reducing the upstream audio queue or changing its wire format.

Reply audio uses the upstream speech buffer and fixed-point Opus 1.6.1 sources in `components/passport_opus`, under their retained BSD license. Speech control frames stay on the BLE worker; I2S writes run only on the speech worker. The volume panel uses the app font and keeps hold-Down recording separate from volume adjustment.

During Muse speech the network worker retires, releasing its 8 KB stack before the original 24 KB decoder task starts. Main supervises restart after playback/cancellation; Wi-Fi and Bluetooth stay connected. This keeps the upstream decoder stack size and avoids a second permanent worker. Public-data cache expiry is reset when its data is released, so returning to Bus immediately refreshes.

History requests follow the upstream allocation strategy: start without a reserved row buffer and grow only as response bytes arrive, within the existing 8 KB bound. Pending/timeout state is independent of whether any bytes arrived. After the voice-note acknowledgement, reply polling waits for the upload task and its queue to retire. This avoids holding the previous full history reservation alongside capture/upload resources; allocation failures remain distinct from oversized responses.
