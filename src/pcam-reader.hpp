/*
Phone Cam
Copyright (C) 2026 Daniel Mwamba

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// PCAM v1 framing (docs/protocol.md). Pure parsing: no OBS, no FFmpeg, no sockets.
namespace pcam {

constexpr size_t kStreamHeaderSize = 16;
constexpr size_t kPacketHeaderSize = 12;
constexpr uint8_t kVersion = 1;
constexpr uint8_t kCodecH264 = 1;

// The size field allows 1 GiB; a 1080p key frame is a few hundred KiB, so anything above this
// is a corrupt stream and must not make us allocate.
constexpr uint32_t kMaxPayloadSize = 8 * 1024 * 1024;

struct StreamHeader {
	uint8_t version = 0;
	uint8_t codec = 0;
	uint16_t width = 0;
	uint16_t height = 0;
	uint16_t fps = 0;
};

struct PacketHeader {
	uint64_t pts_us = 0;
	bool key_frame = false;
	bool config = false;
	uint32_t payload_size = 0;
};

// Blocking byte stream (a TCP socket in the plugin). Returns false on EOF, error or abort.
class ByteSource {
public:
	virtual ~ByteSource() = default;
	virtual bool read_exact(uint8_t *dst, size_t size) = 0;
};

// Parse helpers, exposed separately so they can be checked without a stream.
bool parse_stream_header(const uint8_t (&raw)[kStreamHeaderSize], StreamHeader &out, std::string &error);
bool parse_packet_header(const uint8_t (&raw)[kPacketHeaderSize], PacketHeader &out, std::string &error);

class Reader {
public:
	explicit Reader(ByteSource &source) : source_(source) {}

	bool read_stream_header(StreamHeader &out);
	bool read_packet_header(PacketHeader &out);
	// Reads exactly `size` payload bytes into dst (the caller sized it from the packet header).
	bool read_payload(uint8_t *dst, size_t size);

	const std::string &error() const { return error_; }

private:
	ByteSource &source_;
	std::string error_;
};

} // namespace pcam
