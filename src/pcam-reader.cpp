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

#include "pcam-reader.hpp"

#include <cstring>

namespace pcam {

namespace {

constexpr uint32_t kKeyFrameBit = 1u << 31;
constexpr uint32_t kConfigBit = 1u << 30;
constexpr uint32_t kSizeMask = (1u << 30) - 1;

uint16_t read_u16(const uint8_t *p)
{
	return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

uint64_t read_u64(const uint8_t *p)
{
	return (uint64_t(read_u32(p)) << 32) | read_u32(p + 4);
}

} // namespace

bool parse_stream_header(const uint8_t (&raw)[kStreamHeaderSize], StreamHeader &out, std::string &error)
{
	if (std::memcmp(raw, "PCAM", 4) != 0) {
		error = "bad magic (not a PCAM stream)";
		return false;
	}

	out.version = raw[4];
	out.codec = raw[5];
	out.width = read_u16(raw + 6);
	out.height = read_u16(raw + 8);
	out.fps = read_u16(raw + 10);

	if (out.version != kVersion) {
		error = "unsupported PCAM version " + std::to_string(out.version);
		return false;
	}
	if (out.codec != kCodecH264) {
		error = "unsupported codec " + std::to_string(out.codec);
		return false;
	}
	if (out.width == 0 || out.height == 0) {
		error = "invalid video size";
		return false;
	}
	return true;
}

bool parse_packet_header(const uint8_t (&raw)[kPacketHeaderSize], PacketHeader &out, std::string &error)
{
	const uint32_t flags_size = read_u32(raw + 8);

	out.pts_us = read_u64(raw);
	out.key_frame = (flags_size & kKeyFrameBit) != 0;
	out.config = (flags_size & kConfigBit) != 0;
	out.payload_size = flags_size & kSizeMask;

	if (out.payload_size == 0 || out.payload_size > kMaxPayloadSize) {
		error = "invalid payload size " + std::to_string(out.payload_size);
		return false;
	}
	return true;
}

bool Reader::read_stream_header(StreamHeader &out)
{
	uint8_t raw[kStreamHeaderSize];
	if (!source_.read_exact(raw, sizeof(raw))) {
		error_ = "connection closed before the stream header";
		return false;
	}
	return parse_stream_header(raw, out, error_);
}

bool Reader::read_packet_header(PacketHeader &out)
{
	uint8_t raw[kPacketHeaderSize];
	if (!source_.read_exact(raw, sizeof(raw))) {
		error_ = "connection closed";
		return false;
	}
	return parse_packet_header(raw, out, error_);
}

bool Reader::read_payload(uint8_t *dst, size_t size)
{
	if (!source_.read_exact(dst, size)) {
		error_ = "connection closed inside a packet";
		return false;
	}
	return true;
}

} // namespace pcam
