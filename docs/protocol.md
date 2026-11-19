# Phone Cam stream protocol (PCAM v1)

The phone (Phone Cam app) is the server, the PC (this OBS plugin) is the client. All integers are big-endian.

## Ports

| Port | Content | Used by |
|---|---|---|
| 5050 | PCAM v1 (framed, below) | OBS plugin |
| 5051 | Raw H.264 Annex-B, no framing | `ffplay` / debugging |

Over USB, forward the port first: `adb forward tcp:5050 tcp:5050`.

One viewer per port: a new connection replaces the previous one.

## Stream header (16 bytes, sent once on connect)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | ASCII `PCAM` |
| 4 | 1 | version | `1` |
| 5 | 1 | codec | `1` = H.264 |
| 6 | 2 | width | pixels, e.g. 1920 |
| 8 | 2 | height | pixels, e.g. 1080 |
| 10 | 2 | fps | target frame rate, e.g. 30 |
| 12 | 4 | reserved | `0` |

A client must close the connection if the magic or the version does not match.

## Packets (repeated until the connection closes)

12-byte header, then the payload.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | `pts_us`: presentation time in microseconds (camera clock, monotonic) |
| 8 | 4 | `flags_size`: bit 31 = key frame, bit 30 = codec config, bits 0-29 = payload size in bytes |

Payload: H.264 Annex-B (start codes included), exactly one encoded picture.

Rules:

- The first packet after the stream header is always a key frame.
- Every key frame carries SPS/PPS in front of the picture, so a decoder can start from any key frame. v1 never sends a config packet alone; the config bit is reserved for later codecs.
- When the network falls behind, the phone drops frames up to the next key frame instead of queuing delay.

## Why a framed protocol

With raw Annex-B, a decoder only knows a picture is complete when the next start code arrives, which costs one frame of latency (~33 ms at 30 fps). The size field lets the client hand each picture to the decoder as soon as its last byte arrives, and `pts_us` gives OBS real timestamps.
