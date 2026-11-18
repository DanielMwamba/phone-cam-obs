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

#include "pcam-reader.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

// Blocking Winsock TCP client. Owned and used by one thread; abort() may be called from any
// thread to make a blocked connect() or read_exact() return false right away.
class TcpSocket : public pcam::ByteSource {
public:
	TcpSocket();
	~TcpSocket() override;

	TcpSocket(const TcpSocket &) = delete;
	TcpSocket &operator=(const TcpSocket &) = delete;

	bool connect(const std::string &host, uint16_t port, int timeout_ms);
	bool read_exact(uint8_t *dst, size_t size) override;
	void close();
	void abort();

	const std::string &error() const { return error_; }

private:
	bool wsa_ok_ = false;
	std::mutex mutex_; // guards socket_ between close() and abort()
	uintptr_t socket_; // SOCKET; uintptr_t keeps winsock2.h out of this header
	std::atomic<bool> aborted_{false};
	std::string error_;
};
