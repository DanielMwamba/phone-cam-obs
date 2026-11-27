# Phone Cam stream protocol (PCAM v2)

The phone (Phone Cam app) is the server, the PC (this OBS plugin) is the client. All integers are big-endian.

## Ports

| Port | Content | Used by |
|---|---|---|
| 5050 | PCAM v2 (handshake + framed video, below) | OBS plugin |
| 5051 | Raw H.264 Annex-B, no framing, **localhost only** | `ffplay` / debugging over USB |

Over USB, forward the port first: `adb forward tcp:5050 tcp:5050`.

One viewer per port: a new connection replaces the previous one, but only once it has passed the handshake, so a stranger cannot kick the PC that is live.

## Handshake

### Hello (24 bytes, phone to PC, sent on connect)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | ASCII `PCAM` |
| 4 | 1 | version | `2` |
| 5 | 1 | auth | `0` = none (USB), `1` = pairing code |
| 6 | 2 | reserved | `0` |
| 8 | 16 | nonce | random bytes, new for each connection (zeros when auth is `0`) |

A client must close the connection if the magic or the version does not match.

### Pairing (only when auth is `1`)

The phone shows a 6-digit pairing code; the user types it once in the OBS source.

1. PC to phone, 32 bytes: `HMAC-SHA256(key = the 6 code digits as ASCII, message = "PCAM-PAIR" || nonce)`.
2. Phone to PC, 1 byte: `0` = accepted, `1` = wrong code, `2` = locked (too many wrong codes, try again later). On anything but `0` the phone closes the connection.

The phone gives the client 5 s to answer. After 5 wrong codes in a row it answers `2` to every Wi-Fi client for 60 s. The code never travels in clear and the nonce makes a captured answer useless for the next connection.

Connections from localhost (USB through `adb forward`) get auth `0`: the cable already proves the PC is trusted.

What the pairing does not do: the video itself is not encrypted, so someone capturing the Wi-Fi traffic can still see it, and can try codes offline from one captured handshake (a 6-digit code is only a gate against casual connections, not a cryptographic secret).

## Stream header (16 bytes, after the handshake)

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | magic | ASCII `PCAM` |
| 4 | 1 | version | `2` |
| 5 | 1 | codec | `1` = H.264 |
| 6 | 2 | width | pixels, e.g. 1920 |
| 8 | 2 | height | pixels, e.g. 1080 |
| 10 | 2 | fps | target frame rate, e.g. 30 |
| 12 | 4 | reserved | `0` |

## Packets (repeated until the connection closes)

12-byte header, then the payload.

| Offset | Size | Field |
|---|---|---|
| 0 | 8 | `pts_us`: presentation time in microseconds (camera clock, monotonic) |
| 8 | 4 | `flags_size`: bit 31 = key frame, bit 30 = codec config, bits 0-29 = payload size in bytes |

Payload: H.264 Annex-B (start codes included), exactly one encoded picture.

Rules:

- The first packet after the stream header is always a key frame.
- Every key frame carries SPS/PPS in front of the picture, so a decoder can start from any key frame. The phone never sends a config packet alone; the config bit is reserved for later codecs.
- When the network falls behind, the phone drops frames up to the next key frame instead of queuing delay.

## Versions

- v1: no handshake, the stream header came first. A v1 client reading a v2 phone sees version `2` in the first bytes and stops with "unsupported version".
- v2: adds the hello and the pairing code. Stream header and packets are unchanged.

## Why a framed protocol

With raw Annex-B, a decoder only knows a picture is complete when the next start code arrives, which costs one frame of latency (~33 ms at 30 fps). The size field lets the client hand each picture to the decoder as soon as its last byte arrives, and `pts_us` gives OBS real timestamps.
