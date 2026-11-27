# Phone Cam for OBS

[![Build](https://github.com/DanielMwamba/phone-cam-obs/actions/workflows/build.yml/badge.svg)](https://github.com/DanielMwamba/phone-cam-obs/actions/workflows/build.yml)
[![License: GPL v2](https://img.shields.io/badge/License-GPL%20v2-blue.svg)](LICENSE)
![Platform: Windows x64](https://img.shields.io/badge/Platform-Windows%20x64-0078d4.svg)
![OBS 33](https://img.shields.io/badge/OBS-33.0-302e31.svg)

Use your Android phone as a low-latency 1080p camera in OBS Studio, over Wi-Fi or USB.

This repository is the **OBS side**: a "Phone Cam" video source that receives the live H.264 stream of the **Phone Cam Android app** and shows it in OBS as fast as possible. The Android app is required (public release coming soon).

## Features

- **1080p30 H.264** from the phone's hardware encoder, about 10 Mbps.
- **Low latency**: framed protocol (no waiting for the next frame to find picture boundaries), unbuffered OBS source, a 3-frame queue that skips to the next key frame instead of piling up delay. Measured glass-to-glass: about 190 ms with an entry-level phone (Galaxy A05), most of it in the phone's camera pipeline.
- **USB or Wi-Fi**: in USB mode the plugin runs `adb forward` itself; in Wi-Fi mode you type the address and the pairing code the app shows.
- **Pairing code** over Wi-Fi: only a PC that knows the phone's 6-digit code can connect, and a stranger cannot kick the PC that is live. The code never travels in clear (HMAC challenge-response).
- **Auto-reconnect** every 2 s; the picture is cleared when the phone goes away.
- Software decoding by default, **D3D11VA** hardware decoding as an option.

## Install

1. Download the latest **`phone-cam-<version>-windows-x64-installer.exe`** from [Releases](https://github.com/DanielMwamba/phone-cam-obs/releases) and run it (OBS closed). It installs to `C:\ProgramData\obs-studio\plugins\phone-cam\`.
2. Or take the `.zip` and extract it into `C:\ProgramData\obs-studio\plugins\`.

Requirements: Windows 10/11 x64, OBS Studio 33.x.

## Use

1. Open the Phone Cam app on the phone, in landscape.
2. In OBS: **Sources → + → Phone Cam**.
3. Pick the connection:
   - **USB** (default): enable USB debugging on the phone, plug it in, accept the "Allow USB debugging" prompt. `adb` is taken from your `PATH`, or from the Android SDK (`%LOCALAPPDATA%\Android\Sdk\platform-tools`).
   - **Wi-Fi**: untick **USB**, type the IP and the **pairing code** shown in the app. The phone and the PC must be on the same network, and that network must let devices talk to each other (some public or office Wi-Fi isolate clients; a Windows mobile hotspot works).
4. Port **5050** unless you changed it in the app.

| Setting | Default | What it does |
|---|---|---|
| USB (adb forward) | on | Forwards the port over USB and connects to 127.0.0.1 |
| Host / Port | 127.0.0.1 / 5050 | Phone address for Wi-Fi |
| Pairing code | empty | The 6-digit code shown in the app, Wi-Fi only. After a wrong code the plugin waits until you fix it; after 5 wrong codes the phone refuses Wi-Fi clients for 60 s |
| Hardware decoding | off | D3D11VA; on many laptops software is faster for 1080p30 and shows the first frame sooner |
| Low latency (unbuffered) | on | Show each frame as soon as it is decoded |

Troubleshooting: **Help → Log Files** in OBS; every plugin line starts with `[phone-cam]` (connection, stream header, decoder, fps and decode time every 5 s).

## How it works

```
Phone: camera → hardware H.264 → TCP server :5050 (PCAM v2)
PC:    network thread → 3-packet queue → decode thread (FFmpeg) → OBS async source
```

- The stream format is documented in [docs/protocol.md](docs/protocol.md): a 16-byte stream header, then a 12-byte header (timestamp, size, key-frame flag) in front of each H.264 picture.
- FFmpeg comes from OBS's own DLLs (`avcodec-62`, `avutil-60`); the plugin only links to their import libraries, so it must be built for the matching OBS version.

| File | Role |
|---|---|
| `src/plugin-main.cpp` | Module load/unload, registers the source |
| `src/phonecam-source.*` | The OBS source: properties, network and decode threads, stats logs |
| `src/packet-queue.*` | Bounded packet queue with a buffer pool and drop-to-key-frame rule |
| `src/pcam-reader.*` | PCAM v2 parsing, no OBS dependency |
| `src/pcam-auth.*` | Pairing code response (HMAC-SHA256 from FFmpeg's avutil) |
| `src/tcp-socket.*` | Winsock client: connect timeout, read timeout, abort from another thread |
| `src/h264-decoder.*` | FFmpeg wrapper, D3D11VA then software |
| `src/adb-forward.*` | Runs `adb forward` without a console window |
| `installer/` | Inno Setup script for the Windows installer |

## Build from source

Needs Visual Studio 2022 (or Build Tools) with the Windows 10.0.26100 SDK, and CMake 3.28+.

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64 --config Release
cmake --install build_x64 --config Release   # OBS closed: it locks the DLL
```

The first configure downloads the OBS 33.0.1 sources and the matching obs-deps into `.deps/` and builds libobs (a few minutes). To target another OBS version, update `buildspec.json` (OBS tag, its obs-deps version and both SHA-256).

Releases are built by GitHub Actions: pushing a `v*` tag builds the plugin, the zip and the installer and attaches them to a GitHub release.

## Credits and license

Built from [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). The thread layout and low-latency choices follow the [DroidCam OBS plugin](https://github.com/dev47apps/droidcam-obs-plugin) by DEV47APPS (GPL-2.0).

Licensed under the [GNU GPL v2](LICENSE) or later, like OBS Studio.
