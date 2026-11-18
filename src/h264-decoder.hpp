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

#include <obs.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct AVBufferRef;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

// FFmpeg H.264 decoder tuned for latency. Tries D3D11VA first (frames copied back to system
// memory as NV12, which OBS uploads as-is), then software (I420). Not thread-safe: one decode
// thread owns it.
class H264Decoder {
public:
	// Zeroed bytes the caller must reserve after every payload (FFmpeg's bitstream reader may
	// over-read; equals AV_INPUT_BUFFER_PADDING_SIZE, checked in the .cpp).
	static constexpr size_t kInputPadding = 64;

	enum class Result { Frame, NoFrame, Error };

	H264Decoder();
	~H264Decoder();

	H264Decoder(const H264Decoder &) = delete;
	H264Decoder &operator=(const H264Decoder &) = delete;

	bool open(bool try_hardware);
	void close();

	// data must have kInputPadding zero bytes after `size`. On Frame, `out` points into the
	// decoder's own buffers and stays valid until the next call (OBS copies it on output).
	Result decode(const uint8_t *data, size_t size, obs_source_frame &out);

	bool is_open() const { return context_ != nullptr; }
	bool is_hardware() const { return hw_device_ != nullptr; }
	// "d3d11va" or "software", for logs.
	const char *backend_name() const;
	const std::string &error() const { return error_; }

private:
	bool open_codec(bool use_hardware);
	bool fill_obs_frame(const AVFrame *frame, obs_source_frame &out);

	struct Deleters {
		void operator()(AVCodecContext *p) const;
		void operator()(AVFrame *p) const;
		void operator()(AVPacket *p) const;
		void operator()(AVBufferRef *p) const;
	};

	std::unique_ptr<AVCodecContext, Deleters> context_;
	std::unique_ptr<AVBufferRef, Deleters> hw_device_;
	std::unique_ptr<AVFrame, Deleters> frame_;    // what the codec returns (GPU or CPU)
	std::unique_ptr<AVFrame, Deleters> sw_frame_; // GPU frame copied to system memory
	std::unique_ptr<AVPacket, Deleters> packet_;
	int hw_pix_fmt_ = -1;
	int hw_failures_ = 0;
	bool colors_logged_ = false;
	std::string error_;
};
