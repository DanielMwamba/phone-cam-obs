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

#include "tcp-socket.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>

namespace {

// A live stream sends ~30 packets/s; 3 s of silence means the phone app went to background or
// the cable is gone, so give up and let the reconnect loop take over.
constexpr DWORD kReadTimeoutMs = 3000;
// Room for a couple of key frames so the phone never blocks on a full TCP window.
constexpr int kReceiveBufferBytes = 2 * 1024 * 1024;
constexpr int kAbortPollMs = 100;

std::string wsa_error_text(const char *what)
{
	return std::string(what) + " failed (WSA error " + std::to_string(WSAGetLastError()) + ")";
}

} // namespace

TcpSocket::TcpSocket() : socket_(INVALID_SOCKET)
{
	WSADATA data;
	wsa_ok_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

TcpSocket::~TcpSocket()
{
	close();
	if (wsa_ok_)
		WSACleanup();
}

bool TcpSocket::connect(const std::string &host, uint16_t port, int timeout_ms)
{
	close();
	if (!wsa_ok_) {
		error_ = "WSAStartup failed";
		return false;
	}

	addrinfo hints = {};
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo *addresses = nullptr;
	const std::string port_text = std::to_string(port);
	if (getaddrinfo(host.c_str(), port_text.c_str(), &hints, &addresses) != 0) {
		error_ = wsa_error_text("resolving host");
		return false;
	}

	error_ = "no address";
	for (addrinfo *ai = addresses; ai && !aborted_; ai = ai->ai_next) {
		SOCKET s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (s == INVALID_SOCKET) {
			error_ = wsa_error_text("socket");
			continue;
		}

		// Non-blocking connect so a dead Wi-Fi host does not hold the thread for the OS
		// default (~21 s), and so abort() is noticed quickly.
		u_long non_blocking = 1;
		ioctlsocket(s, FIONBIO, &non_blocking);

		bool connected = false;
		if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
			connected = true;
		} else if (WSAGetLastError() == WSAEWOULDBLOCK) {
			for (int waited = 0; waited < timeout_ms && !aborted_; waited += kAbortPollMs) {
				fd_set writable, failed;
				FD_ZERO(&writable);
				FD_ZERO(&failed);
				FD_SET(s, &writable);
				FD_SET(s, &failed);
				timeval tv = {0, kAbortPollMs * 1000};
				if (select(0, nullptr, &writable, &failed, &tv) > 0) {
					connected = FD_ISSET(s, &writable) != 0;
					break;
				}
			}
			error_ = connected ? "" : "connection refused or timed out";
		} else {
			error_ = wsa_error_text("connect");
		}

		if (!connected || aborted_) {
			closesocket(s);
			continue;
		}

		non_blocking = 0;
		ioctlsocket(s, FIONBIO, &non_blocking);

		DWORD read_timeout = kReadTimeoutMs;
		setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&read_timeout),
			   sizeof(read_timeout));
		int receive_buffer = kReceiveBufferBytes;
		setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&receive_buffer),
			   sizeof(receive_buffer));

		{
			std::lock_guard<std::mutex> lock(mutex_);
			socket_ = s;
		}
		freeaddrinfo(addresses);
		error_.clear();
		return true;
	}

	freeaddrinfo(addresses);
	if (aborted_)
		error_ = "aborted";
	return false;
}

bool TcpSocket::read_exact(uint8_t *dst, size_t size)
{
	const SOCKET s = static_cast<SOCKET>(socket_);
	if (s == INVALID_SOCKET)
		return false;

	while (size > 0) {
		const int chunk = static_cast<int>(std::min<size_t>(size, 1 << 20));
		const int received = recv(s, reinterpret_cast<char *>(dst), chunk, 0);
		if (received <= 0) {
			if (received == 0)
				error_ = "closed by peer";
			else if (WSAGetLastError() == WSAETIMEDOUT)
				error_ = "no data for 3 s";
			else
				error_ = wsa_error_text("recv");
			return false;
		}
		dst += received;
		size -= static_cast<size_t>(received);
	}
	return true;
}

void TcpSocket::close()
{
	SOCKET s;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		s = static_cast<SOCKET>(socket_);
		socket_ = INVALID_SOCKET;
	}
	if (s != INVALID_SOCKET)
		closesocket(s);
}

void TcpSocket::abort()
{
	aborted_ = true;
	// shutdown() wakes up a recv() blocked in the network thread; that thread still owns the
	// handle and closes it. The lock keeps us from touching a handle that was just closed.
	std::lock_guard<std::mutex> lock(mutex_);
	const SOCKET s = static_cast<SOCKET>(socket_);
	if (s != INVALID_SOCKET)
		shutdown(s, SD_BOTH);
}
