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

#include "packet-queue.hpp"
#include "tcp-socket.hpp"

#include <obs-module.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

void register_phonecam_source();

// One "Phone Cam" source in OBS. Two threads per source:
// - network: connect (with adb forward over USB), read PCAM packets, reconnect every 2 s;
// - decode: decode H.264 and hand each picture to OBS the moment it is ready.
// Splitting them keeps a slow decode from stalling the socket (the phone would then drop
// frames itself) and lets the bounded queue between them shed latency.
class PhoneCamSource {
public:
	PhoneCamSource(obs_data_t *settings, obs_source_t *source);
	~PhoneCamSource();

	PhoneCamSource(const PhoneCamSource &) = delete;
	PhoneCamSource &operator=(const PhoneCamSource &) = delete;

	void update(obs_data_t *settings);

private:
	struct Config {
		std::string host;
		uint16_t port = 0;
		std::string pairing_code; // 6 digits, empty when not typed or malformed
		bool usb = true;
		bool hardware_decode = true;
		bool unbuffered = true;

		bool same_connection(const Config &other) const
		{
			return host == other.host && port == other.port && usb == other.usb &&
			       pairing_code == other.pairing_code && hardware_decode == other.hardware_decode;
		}
	};

	static Config read_config(obs_data_t *settings);

	void start();
	void stop();
	enum class PairingOutcome { Paired, RetryLater, NeedNewCode };

	void network_loop(Config config);
	PairingOutcome pair(pcam::Reader &reader, const pcam::Hello &hello, const Config &config,
			    std::string &message);
	void decode_loop(bool try_hardware);
	// Returns false if the source is stopping (sleep interrupted).
	bool sleep_unless_stopped(int milliseconds);
	// Blocks until the source stops or its settings change (which restarts the threads).
	void wait_for_new_settings();

	obs_source_t *const source_;
	Config config_;

	std::atomic<bool> running_{false};
	std::unique_ptr<TcpSocket> socket_;
	PacketQueue queue_;
	std::thread network_thread_;
	std::thread decode_thread_;

	std::mutex sleep_mutex_;
	std::condition_variable sleep_cv_;
};
