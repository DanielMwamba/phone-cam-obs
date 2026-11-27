/*
Phone Cam
Copyright (C) 2026 Daniel Mwamba

The thread layout (network thread + decode thread, packet pool, reconnect loop) follows the
DroidCam OBS plugin (src/source.cc), Copyright (C) 2021 DEV47APPS, github.com/dev47apps

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

#include "phonecam-source.hpp"

#include "adb-forward.hpp"
#include "h264-decoder.hpp"
#include "pcam-auth.hpp"
#include "pcam-reader.hpp"

#include <plugin-support.h>
#include <util/platform.h>
#include <util/threading.h>

#include <cstring>

namespace {

constexpr const char *kHost = "host";
constexpr const char *kPort = "port";
constexpr const char *kPairingCode = "pairing_code";
constexpr const char *kUsb = "usb";
constexpr const char *kHardwareDecode = "hw_decode";
constexpr const char *kUnbuffered = "unbuffered";
// Saved with the source, so a disconnected source stays disconnected after an OBS restart.
constexpr const char *kEnabled = "enabled";
constexpr const char *kToggle = "toggle_connection";

constexpr const char *kLocalhost = "127.0.0.1";
constexpr int kDefaultPort = 5050;
constexpr int kReconnectDelayMs = 2000;
constexpr int kConnectTimeoutMs = 2000;
// The phone refuses every Wi-Fi client for 60 s after 5 wrong codes.
constexpr int kLockedRetryMs = 60000;
constexpr uint64_t kStatsIntervalNs = 5000000000ULL;

// 3 pictures = 100 ms at 30 fps. Enough to absorb Wi-Fi jitter; beyond that we would rather
// drop to the next key frame than show an old picture.
constexpr size_t kQueueCapacity = 3;

} // namespace

PhoneCamSource::PhoneCamSource(obs_data_t *settings, obs_source_t *source)
	: source_(source),
	  config_(read_config(settings)),
	  queue_(kQueueCapacity)
{
	// The main latency switch: without it OBS holds async frames in a jitter buffer and plays
	// them back on their timestamps, which adds a frame or more of delay.
	obs_source_set_async_unbuffered(source_, config_.unbuffered);
	start();
}

PhoneCamSource::~PhoneCamSource()
{
	stop();
}

PhoneCamSource::Config PhoneCamSource::read_config(obs_data_t *settings)
{
	Config config;
	config.host = obs_data_get_string(settings, kHost);
	config.port = static_cast<uint16_t>(obs_data_get_int(settings, kPort));
	config.usb = obs_data_get_bool(settings, kUsb);
	if (!pcam::normalize_pairing_code(obs_data_get_string(settings, kPairingCode), config.pairing_code))
		config.pairing_code.clear();
	config.hardware_decode = obs_data_get_bool(settings, kHardwareDecode);
	config.unbuffered = obs_data_get_bool(settings, kUnbuffered);
	config.enabled = obs_data_get_bool(settings, kEnabled);
	if (config.usb || config.host.empty())
		config.host = kLocalhost;
	return config;
}

void PhoneCamSource::update(obs_data_t *settings)
{
	const Config config = read_config(settings);

	if (config.unbuffered != config_.unbuffered)
		obs_source_set_async_unbuffered(source_, config.unbuffered);

	if (config.same_connection(config_)) {
		config_ = config;
		return;
	}
	stop();
	config_ = config;
	start();
}

void PhoneCamSource::start()
{
	if (!config_.enabled)
		return;
	running_ = true;
	socket_ = std::make_unique<TcpSocket>();
	queue_.restart();
	// Each thread gets its own copy of the settings: update() may change config_ meanwhile.
	network_thread_ = std::thread(&PhoneCamSource::network_loop, this, config_);
	decode_thread_ = std::thread(&PhoneCamSource::decode_loop, this, config_.hardware_decode);
}

void PhoneCamSource::stop()
{
	if (!running_)
		return;

	{
		// Under the lock: otherwise the network thread can test running_, miss the notify,
		// and sleep a full reconnect delay before noticing the stop.
		std::lock_guard<std::mutex> lock(sleep_mutex_);
		running_ = false;
	}
	sleep_cv_.notify_all();
	if (socket_)
		socket_->abort();
	queue_.stop();

	if (network_thread_.joinable())
		network_thread_.join();
	if (decode_thread_.joinable())
		decode_thread_.join();
	socket_.reset();

	obs_source_output_video(source_, nullptr);
}

void PhoneCamSource::wait_for_new_settings()
{
	std::unique_lock<std::mutex> lock(sleep_mutex_);
	sleep_cv_.wait(lock, [this] { return !running_; });
}

PhoneCamSource::PairingOutcome PhoneCamSource::pair(pcam::Reader &reader, const pcam::Hello &hello,
						    const Config &config, std::string &message)
{
	if (config.pairing_code.empty()) {
		message = "the phone asks for a pairing code: type the 6-digit code shown in the app";
		return PairingOutcome::NeedNewCode;
	}

	uint8_t response[pcam::kPairingResponseSize];
	pcam::pairing_response(config.pairing_code, hello.nonce, response);
	if (!socket_->write_all(response, sizeof(response))) {
		message = "pairing interrupted: " + socket_->error();
		return PairingOutcome::RetryLater;
	}
	pcam::PairingStatus status;
	if (!reader.read_pairing_status(status)) {
		message = "pairing interrupted: " + reader.error();
		return PairingOutcome::RetryLater;
	}

	switch (status) {
	case pcam::PairingStatus::Ok:
		return PairingOutcome::Paired;
	case pcam::PairingStatus::WrongCode:
		// Retrying the same code would only lock the phone: wait for the user to fix it.
		message = "wrong pairing code: check the code shown in the app";
		return PairingOutcome::NeedNewCode;
	case pcam::PairingStatus::Locked:
	default:
		message = "the phone is locked after too many wrong codes, retrying in 60 s";
		return PairingOutcome::RetryLater;
	}
}

bool PhoneCamSource::sleep_unless_stopped(int milliseconds)
{
	std::unique_lock<std::mutex> lock(sleep_mutex_);
	sleep_cv_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return !running_; });
	return running_;
}

void PhoneCamSource::network_loop(const Config config)
{
	os_set_thread_name("phone-cam: network");

	uint64_t stream_id = 0;
	std::string last_error; // log each distinct failure once, not every 2 s

	auto report = [&last_error](const std::string &message) {
		if (message != last_error)
			obs_log(LOG_INFO, "%s", message.c_str());
		last_error = message;
	};

	while (running_) {
		if (config.usb) {
			std::string error;
			if (!adb_forward(config.port, running_, error)) {
				report("adb forward: " + error);
				sleep_unless_stopped(kReconnectDelayMs);
				continue;
			}
		}

		if (!socket_->connect(config.host, config.port, kConnectTimeoutMs)) {
			report("cannot connect to " + config.host + ":" + std::to_string(config.port) + ": " +
			       socket_->error());
			sleep_unless_stopped(kReconnectDelayMs);
			continue;
		}

		const uint64_t connected_ns = os_gettime_ns();
		pcam::Reader reader(*socket_);
		pcam::Hello hello;
		if (!reader.read_hello(hello)) {
			// Over USB the adb port always accepts, even when the app is closed; the
			// failure shows up here instead.
			report("no stream from " + config.host + ":" + std::to_string(config.port) + " (" +
			       reader.error() + "); is the Phone Cam app open?");
			socket_->close();
			sleep_unless_stopped(kReconnectDelayMs);
			continue;
		}

		if (hello.auth == pcam::kAuthPairingCode) {
			std::string message;
			const PairingOutcome outcome = pair(reader, hello, config, message);
			if (outcome != PairingOutcome::Paired) {
				report(message);
				socket_->close();
				if (outcome == PairingOutcome::NeedNewCode)
					wait_for_new_settings();
				else
					sleep_unless_stopped(kLockedRetryMs);
				continue;
			}
		}

		pcam::StreamHeader header;
		if (!reader.read_stream_header(header)) {
			report("no stream from " + config.host + ":" + std::to_string(config.port) + " (" +
			       reader.error() + ")");
			socket_->close();
			sleep_unless_stopped(kReconnectDelayMs);
			continue;
		}

		last_error.clear();
		stream_id++;
		obs_log(LOG_INFO, "connected to %s:%u%s: PCAM v%u H.264 %ux%u @ %u fps", config.host.c_str(),
			config.port, hello.auth == pcam::kAuthPairingCode ? " (paired)" : "", header.version,
			header.width, header.height, header.fps);

		size_t dropped = 0;
		pcam::PacketHeader packet_header;
		while (running_ && reader.read_packet_header(packet_header)) {
			std::unique_ptr<Packet> packet = queue_.acquire();
			packet->data.resize(packet_header.payload_size + H264Decoder::kInputPadding);
			if (!reader.read_payload(packet->data.data(), packet_header.payload_size))
				break;
			std::memset(packet->data.data() + packet_header.payload_size, 0, H264Decoder::kInputPadding);

			packet->size = packet_header.payload_size;
			// Config-only packets (reserved in v1) must never be dropped either.
			packet->key_frame = packet_header.key_frame || packet_header.config;
			packet->stream_id = stream_id;
			packet->stream_start_ns = connected_ns;

			const size_t dropped_now = queue_.push(std::move(packet));
			if (dropped_now > 0 && dropped == 0)
				obs_log(LOG_WARNING, "decoder behind: dropping to the next key frame");
			dropped += dropped_now;
		}

		if (running_)
			obs_log(LOG_INFO, "disconnected: %s (%zu packets dropped)", reader.error().c_str(), dropped);
		socket_->close();
		// Through the queue rather than directly, so a frame still being decoded cannot
		// reappear after the picture was cleared.
		queue_.push_end_of_stream();
		sleep_unless_stopped(kReconnectDelayMs);
	}
}

void PhoneCamSource::decode_loop(const bool try_hardware)
{
	os_set_thread_name("phone-cam: decode");

	H264Decoder decoder;
	obs_source_frame frame = {};
	uint64_t stream_id = 0;
	bool first_frame_pending = false;

	uint64_t stats_start_ns = os_gettime_ns();
	uint64_t stats_decode_ns = 0;
	uint32_t stats_frames = 0;
	uint32_t errors_logged = 0;

	while (std::unique_ptr<Packet> packet = queue_.pop()) {
		if (packet->end_of_stream) {
			decoder.close();
			obs_source_output_video(source_, nullptr);
			queue_.recycle(std::move(packet));
			continue;
		}

		if (packet->stream_id != stream_id || !decoder.is_open()) {
			stream_id = packet->stream_id;
			if (!decoder.open(try_hardware)) {
				obs_log(LOG_ERROR, "cannot open H.264 decoder: %s", decoder.error().c_str());
				queue_.recycle(std::move(packet));
				continue;
			}
			if (try_hardware && !decoder.is_hardware())
				obs_log(LOG_WARNING, "hardware decoding unavailable (%s)", decoder.error().c_str());
			obs_log(LOG_INFO, "decoder: %s", decoder.backend_name());
			first_frame_pending = true;
			errors_logged = 0;
			// Count from this stream's start, not from the idle time before it.
			stats_start_ns = os_gettime_ns();
			stats_decode_ns = 0;
			stats_frames = 0;
		}

		const bool was_hardware = decoder.is_hardware();
		const uint64_t start_ns = os_gettime_ns();
		const H264Decoder::Result result = decoder.decode(packet->data.data(), packet->size, frame);
		const uint64_t end_ns = os_gettime_ns();

		if (result == H264Decoder::Result::Frame) {
			// Monotonic arrival time. In unbuffered mode OBS shows the frame now anyway; in
			// buffered mode it at least keeps a steady clock.
			frame.timestamp = end_ns;
			obs_source_output_video(source_, &frame);

			stats_frames++;
			stats_decode_ns += end_ns - start_ns;
			if (first_frame_pending) {
				first_frame_pending = false;
				obs_log(LOG_INFO, "first frame %.0f ms after connect",
					double(end_ns - packet->stream_start_ns) / 1e6);
			}
		} else if (result == H264Decoder::Result::Error && errors_logged < 5) {
			errors_logged++;
			obs_log(LOG_WARNING, "decode error: %s", decoder.error().c_str());
		}
		if (was_hardware && !decoder.is_hardware())
			obs_log(LOG_WARNING, "decoder: switched to software");

		queue_.recycle(std::move(packet));

		const uint64_t now_ns = os_gettime_ns();
		if (now_ns - stats_start_ns >= kStatsIntervalNs) {
			const double seconds = double(now_ns - stats_start_ns) / 1e9;
			if (stats_frames > 0)
				obs_log(LOG_INFO, "%.1f fps, decode %.2f ms/frame (%s)", stats_frames / seconds,
					double(stats_decode_ns) / stats_frames / 1e6, decoder.backend_name());
			stats_start_ns = now_ns;
			stats_decode_ns = 0;
			stats_frames = 0;
		}
	}
}

// OBS glue -------------------------------------------------------------------------------

namespace {

const char *toggle_label(bool enabled)
{
	return obs_module_text(enabled ? "Disconnect" : "Connect");
}

bool toggle_clicked(obs_properties_t *, obs_property_t *property, void *data)
{
	auto *self = static_cast<PhoneCamSource *>(data);
	const bool enable = !self->enabled();

	// Through the settings, like any other field: update() stops or starts the threads and
	// the choice is saved with the scene.
	obs_source_t *source = self->source();
	obs_data_t *settings = obs_source_get_settings(source);
	obs_data_set_bool(settings, kEnabled, enable);
	obs_source_update(source, settings);
	obs_data_release(settings);

	obs_property_set_description(property, toggle_label(enable));
	return true;
}

bool usb_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	// Over USB the host is always 127.0.0.1 (adb forward) and no code is asked, so these
	// fields would only confuse.
	const bool wifi = !obs_data_get_bool(settings, kUsb);
	obs_property_set_enabled(obs_properties_get(props, kHost), wifi);
	obs_property_set_enabled(obs_properties_get(props, kPairingCode), wifi);
	return true;
}

obs_properties_t *get_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	const bool enabled = static_cast<PhoneCamSource *>(data)->enabled();
	obs_properties_add_button2(props, kToggle, toggle_label(enabled), toggle_clicked, data);

	obs_property_t *usb = obs_properties_add_bool(props, kUsb, obs_module_text("Usb"));
	obs_property_set_long_description(usb, obs_module_text("Usb.Description"));
	obs_property_set_modified_callback(usb, usb_modified);

	obs_properties_add_text(props, kHost, obs_module_text("Host"), OBS_TEXT_DEFAULT);
	obs_property_t *code = obs_properties_add_text(props, kPairingCode, obs_module_text("PairingCode"),
							OBS_TEXT_DEFAULT);
	obs_property_set_long_description(code, obs_module_text("PairingCode.Description"));
	obs_properties_add_int(props, kPort, obs_module_text("Port"), 1, 65535, 1);
	obs_properties_add_bool(props, kHardwareDecode, obs_module_text("HardwareDecode"));

	obs_property_t *unbuffered = obs_properties_add_bool(props, kUnbuffered, obs_module_text("Unbuffered"));
	obs_property_set_long_description(unbuffered, obs_module_text("Unbuffered.Description"));
	return props;
}

void get_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, kHost, kLocalhost);
	obs_data_set_default_string(settings, kPairingCode, "");
	obs_data_set_default_int(settings, kPort, kDefaultPort);
	obs_data_set_default_bool(settings, kUsb, true);
	// Off by default: measured on a Ryzen 5 4650U, software decoding of 1080p30 takes ~6 ms per
	// frame versus ~7 ms for D3D11VA (which must copy each frame back to RAM), and D3D11VA
	// needs ~350 ms to show the first frame after every (re)connect versus ~30 ms.
	obs_data_set_default_bool(settings, kHardwareDecode, false);
	obs_data_set_default_bool(settings, kUnbuffered, true);
	obs_data_set_default_bool(settings, kEnabled, true);
}

} // namespace

void register_phonecam_source()
{
	obs_source_info info = {};
	info.id = "phonecam_source";
	info.type = OBS_SOURCE_TYPE_INPUT;
	// DO_NOT_DUPLICATE: the phone serves one viewer per port, a copy would steal the stream.
	info.output_flags = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_DO_NOT_DUPLICATE;
	info.icon_type = OBS_ICON_TYPE_CAMERA;
	info.get_name = [](void *) {
		return obs_module_text("PhoneCam");
	};
	info.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
		return new PhoneCamSource(settings, source);
	};
	info.destroy = [](void *data) {
		delete static_cast<PhoneCamSource *>(data);
	};
	info.update = [](void *data, obs_data_t *settings) {
		static_cast<PhoneCamSource *>(data)->update(settings);
	};
	info.get_defaults = get_defaults;
	info.get_properties = get_properties;
	obs_register_source(&info);
}
