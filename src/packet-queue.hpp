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

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

struct Packet {
	std::vector<uint8_t> data; // payload followed by decoder padding; capacity is reused
	size_t size = 0;           // payload bytes, without padding
	bool key_frame = false;
	uint64_t pts_us = 0;        // phone clock, to line the sound up with this picture
	bool end_of_stream = false; // marker: connection lost, clear the picture
	uint64_t stream_id = 0;     // changes on every connection, so the decoder starts fresh
	uint64_t stream_start_ns = 0;
};

// Bounded hand-off from the network thread to the decode thread, with a pool of packets so a
// steady stream allocates nothing.
//
// Latency rule: the queue never grows. When it is full, everything queued is dropped and the
// following P-frames are skipped until the next key frame (one per second from the phone).
// A short freeze is better than a picture that stays seconds behind.
class PacketQueue {
public:
	explicit PacketQueue(size_t capacity) : capacity_(capacity) {}

	std::unique_ptr<Packet> acquire();
	void recycle(std::unique_ptr<Packet> packet);

	// Returns how many packets were dropped by this call (0 in steady state).
	size_t push(std::unique_ptr<Packet> packet);
	// Drops what is queued and tells the decode thread the stream ended.
	void push_end_of_stream();
	// Blocks until a packet is ready; returns nullptr once stop() was called.
	std::unique_ptr<Packet> pop();

	void stop();
	void restart();

private:
	void drop_all_locked();

	const size_t capacity_;
	std::mutex mutex_;
	std::condition_variable ready_;
	std::deque<std::unique_ptr<Packet>> queue_;
	std::vector<std::unique_ptr<Packet>> pool_;
	bool waiting_for_key_frame_ = false;
	bool stopped_ = false;
};
