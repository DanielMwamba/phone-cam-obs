# Phone Cam OBS plugin

OBS Studio source ("Phone Cam") that shows the live H.264 stream of the Phone Cam Android app with minimal latency. Windows x64, built for OBS Studio 33.0.1. Speaks PCAM v1 ([../docs/protocol.md](../docs/protocol.md)).

Based on [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) (Windows parts only). Ideas from the [DroidCam OBS plugin](https://github.com/dev47apps/droidcam-obs-plugin) (GPL-2.0). License: GPL-2.0 (see `LICENSE`).

## How it works

- **Network thread**: optional `adb forward tcp:<port> tcp:<port>` (USB), connects, checks the 16-byte stream header, reads framed packets. Reconnects every 2 s; on disconnect the picture is cleared.
- **Decode thread**: FFmpeg H.264 with D3D11VA (frames copied back to NV12), software fallback (I420). `AV_CODEC_FLAG_LOW_DELAY` + `AV_CODEC_FLAG2_FAST`, no frame threading, no swscale: OBS gets the decoder's planes directly.
- Between them, a queue of 3 packets (100 ms). If the decoder falls behind, the queue is emptied and P-frames are skipped up to the next key frame (the phone sends one per second) instead of building delay.
- The source is unbuffered by default (`obs_source_set_async_unbuffered`): each frame is shown as soon as it is decoded.
- FFmpeg comes from OBS's own DLLs (`avcodec-62`, `avutil-60`); the plugin only links to their import libraries.

| File | Role |
|---|---|
| `src/plugin-main.cpp` | Module load/unload, registers the source |
| `src/phonecam-source.*` | The OBS source: properties, network and decode threads, stats logs |
| `src/packet-queue.*` | Bounded packet queue with a buffer pool and drop-to-key-frame rule |
| `src/pcam-reader.*` | PCAM v1 parsing (stream header, packet header, size checks), no OBS dependency |
| `src/tcp-socket.*` | Winsock client: connect timeout, 3 s read timeout, abort from another thread |
| `src/h264-decoder.*` | FFmpeg wrapper, D3D11VA then software, outputs `obs_source_frame` |
| `src/adb-forward.*` | Runs `adb forward` (adb from PATH, else the Android SDK) |
| `data/locale/*.ini` | UI strings (en-US, fr-FR) |

## Build

Needs Visual Studio 2022 (or Build Tools) with the Windows 10.0.26100 SDK and CMake 3.28+.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config Release
```

The first configure downloads obs-deps 2026-10-23 and the OBS 33.0.1 sources into `.deps/`, then builds libobs (a few minutes). Output: `build_x64/Release/phone-cam.dll`.

To build against another OBS version, update `buildspec.json` (OBS tag and its `obs-deps` version, from that tag's `CMakePresets.json`, plus the SHA-256 of both zips). The FFmpeg major versions must match the `avcodec-*.dll` in `C:\Program Files\obs-studio\bin\64bit`.

## Install

Copy to OBS's per-machine plugin folder (no admin needed when `C:\ProgramData\obs-studio` is yours):

```
C:\ProgramData\obs-studio\plugins\phone-cam\bin\64bit\phone-cam.dll  (+ phone-cam.pdb)
C:\ProgramData\obs-studio\plugins\phone-cam\data\locale\*.ini
```

Or `cmake --install build_x64 --config Release` (the template's default prefix is that same folder). OBS 33 also accepts the newer flat layout (`plugins\phone-cam\phone-cam.dll`); the `bin\64bit` layout above is loaded as a legacy plugin.

## Use

1. Open the Phone Cam app on the phone (landscape). Over USB: USB debugging on, phone plugged in.
2. In OBS: Sources, +, **Phone Cam**.
3. Properties:
   - **USB (adb forward)**, on by default: forwards the port and connects to 127.0.0.1. Turn it off for Wi-Fi and type the IP shown in the app.
   - **Port**: 5050.
   - **Hardware decoding**: D3D11VA, falls back to software by itself.
   - **Low latency (unbuffered)**: on by default; turn off only for smoother playback on a bad Wi-Fi.
4. Logs (Help, Log Files) show `[phone-cam]` lines: connection, stream header, decoder (d3d11va or software), and every 5 s the fps and decode time.
