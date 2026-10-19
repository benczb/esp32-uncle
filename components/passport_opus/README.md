[简体中文](README.zh_CN.md) · **English**

# Opus reply-speech decoder

Fixed-point Opus 1.6.1 and the Passport wrapper are copied from
[FalkoWing/muse-passport](https://github.com/FalkoWing/muse-passport), commit
`664f5c2a3c9fb9c0b0bf4ed9801907c7f01fdb3e`, `shared/opus`.
The source and copyright notices are unchanged; see [COPYING](COPYING).
Only the CMake paths are adapted. The Muse-enabled app uses the upstream
16 kHz mono / 60 ms / 120-byte packet decoder; unused encoder code is removed
by the firmware linker. Decoder RAM and playback stack exist only during speech.
