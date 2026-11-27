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

#include <atomic>
#include <cstdint>

// Maps the phone's clock (packet pts) to OBS's clock, so the sound plays when the matching
// picture shows. The offset is learned from the video: for each picture handed to OBS,
// offset = shown_at - pts. The smallest value is kept (the picture that had the least network
// and decode delay), re-measured every few seconds so clock drift between the phone and the PC
// cannot pile up.
//
// reset() and to_obs_time() come from the network thread, observe() from the decode thread.
class PtsClock {
public:
	// A new connection: forget the old mapping until a picture of this stream shows.
	void reset(uint64_t stream_id);
	void observe(uint64_t stream_id, uint64_t pts_us, uint64_t shown_ns);
	// False until the first picture of the current stream was shown.
	bool to_obs_time(uint64_t pts_us, uint64_t &obs_ns) const;

private:
	static constexpr int64_t kUnset = INT64_MIN;

	std::atomic<uint64_t> stream_id_{0};
	std::atomic<int64_t> offset_ns_{kUnset};

	// Decode thread only.
	uint64_t window_stream_id_ = 0;
	uint64_t window_start_ns_ = 0;
	int64_t window_min_ns_ = INT64_MAX;
};
