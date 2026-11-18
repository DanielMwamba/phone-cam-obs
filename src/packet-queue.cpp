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

#include "packet-queue.hpp"

std::unique_ptr<Packet> PacketQueue::acquire()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!pool_.empty()) {
			std::unique_ptr<Packet> packet = std::move(pool_.back());
			pool_.pop_back();
			// Pooled packets may come back from any path (dropped, marker); reset all but data.
			packet->size = 0;
			packet->key_frame = false;
			packet->end_of_stream = false;
			return packet;
		}
	}
	return std::make_unique<Packet>();
}

void PacketQueue::recycle(std::unique_ptr<Packet> packet)
{
	if (!packet)
		return;

	std::lock_guard<std::mutex> lock(mutex_);
	// Keep enough buffers for a full queue plus the ones in flight; free the rest.
	if (pool_.size() < capacity_ + 2)
		pool_.push_back(std::move(packet));
}

size_t PacketQueue::push(std::unique_ptr<Packet> packet)
{
	size_t dropped = 0;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (stopped_)
			return 0;

		if (packet->key_frame) {
			// A key frame is a clean restart point: whatever is still queued is now stale.
			if (queue_.size() >= capacity_) {
				dropped = queue_.size();
				drop_all_locked();
			}
			waiting_for_key_frame_ = false;
		} else if (waiting_for_key_frame_ || queue_.size() >= capacity_) {
			// P-frames reference the frames we dropped, so they cannot be decoded either.
			dropped = queue_.size() + 1;
			drop_all_locked();
			waiting_for_key_frame_ = true;
			if (pool_.size() < capacity_ + 2)
				pool_.push_back(std::move(packet));
			return dropped;
		}
		queue_.push_back(std::move(packet));
	}
	ready_.notify_one();
	return dropped;
}

void PacketQueue::push_end_of_stream()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		drop_all_locked();
		waiting_for_key_frame_ = false;
		auto marker = std::make_unique<Packet>();
		marker->end_of_stream = true;
		queue_.push_back(std::move(marker));
	}
	ready_.notify_one();
}

std::unique_ptr<Packet> PacketQueue::pop()
{
	std::unique_lock<std::mutex> lock(mutex_);
	ready_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
	if (stopped_)
		return nullptr;
	std::unique_ptr<Packet> packet = std::move(queue_.front());
	queue_.pop_front();
	return packet;
}

void PacketQueue::stop()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stopped_ = true;
		drop_all_locked();
	}
	ready_.notify_all();
}

void PacketQueue::restart()
{
	std::lock_guard<std::mutex> lock(mutex_);
	drop_all_locked();
	waiting_for_key_frame_ = false;
	stopped_ = false;
}

void PacketQueue::drop_all_locked()
{
	while (!queue_.empty()) {
		if (pool_.size() < capacity_ + 2)
			pool_.push_back(std::move(queue_.front()));
		queue_.pop_front();
	}
}
