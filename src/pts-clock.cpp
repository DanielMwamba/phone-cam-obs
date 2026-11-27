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

#include "pts-clock.hpp"

#include <algorithm>

namespace {

constexpr uint64_t kWindowNs = 3000000000ULL;

} // namespace

void PtsClock::reset(uint64_t stream_id)
{
	stream_id_ = stream_id;
	offset_ns_ = kUnset;
}

void PtsClock::observe(uint64_t stream_id, uint64_t pts_us, uint64_t shown_ns)
{
	if (stream_id != stream_id_)
		return;

	const int64_t offset = static_cast<int64_t>(shown_ns) - static_cast<int64_t>(pts_us * 1000);
	if (stream_id != window_stream_id_ || offset_ns_ == kUnset) {
		window_stream_id_ = stream_id;
		window_start_ns_ = shown_ns;
		window_min_ns_ = offset;
		offset_ns_ = offset;
		return;
	}

	window_min_ns_ = std::min(window_min_ns_, offset);
	// A faster picture lowers the delay at once; a slower clock only after a full window,
	// so one late picture never pushes the sound back.
	if (offset < offset_ns_)
		offset_ns_ = offset;
	if (shown_ns - window_start_ns_ >= kWindowNs) {
		offset_ns_ = window_min_ns_;
		window_start_ns_ = shown_ns;
		window_min_ns_ = INT64_MAX;
	}
}

bool PtsClock::to_obs_time(uint64_t pts_us, uint64_t &obs_ns) const
{
	const int64_t offset = offset_ns_;
	if (offset == kUnset)
		return false;
	obs_ns = static_cast<uint64_t>(static_cast<int64_t>(pts_us * 1000) + offset);
	return true;
}
