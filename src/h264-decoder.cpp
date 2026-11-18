/*
Phone Cam
Copyright (C) 2026 Daniel Mwamba

Hardware device selection and the FFmpeg-to-OBS format mapping follow the
DroidCam OBS plugin (src/ffmpeg_decode.cc):
Copyright (C) 2014 by Hugh Bailey <obs.jim@gmail.com>
Copyright (C) 2021 DEV47APPS, github.com/dev47apps

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

#include "h264-decoder.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

#include <cstring>

static_assert(H264Decoder::kInputPadding >= AV_INPUT_BUFFER_PADDING_SIZE, "payload padding too small for FFmpeg");

namespace {

// After this many hardware errors in a row, the GPU path is assumed broken (driver, profile)
// and the decoder is reopened in software.
constexpr int kMaxHardwareFailures = 3;

std::string av_error_text(int code)
{
	char text[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(code, text, sizeof(text));
	return text;
}

video_format to_obs_format(int format)
{
	switch (format) {
	case AV_PIX_FMT_NV12:
		return VIDEO_FORMAT_NV12;
	case AV_PIX_FMT_YUV420P:
	case AV_PIX_FMT_YUVJ420P:
		return VIDEO_FORMAT_I420;
	case AV_PIX_FMT_YUV422P:
	case AV_PIX_FMT_YUVJ422P:
		return VIDEO_FORMAT_I422;
	default:
		return VIDEO_FORMAT_NONE;
	}
}

video_colorspace to_obs_colorspace(const AVFrame *frame)
{
	switch (frame->colorspace) {
	case AVCOL_SPC_BT709:
		return VIDEO_CS_709;
	case AVCOL_SPC_FCC:
	case AVCOL_SPC_BT470BG:
	case AVCOL_SPC_SMPTE170M:
	case AVCOL_SPC_SMPTE240M:
		return VIDEO_CS_601;
	default:
		// Unsigned streams: HD content is BT.709 by convention.
		return frame->height >= 720 ? VIDEO_CS_709 : VIDEO_CS_601;
	}
}

AVPixelFormat pick_format(AVCodecContext *context, const AVPixelFormat *offered)
{
	const auto wanted = static_cast<AVPixelFormat>(reinterpret_cast<intptr_t>(context->opaque));
	for (const AVPixelFormat *f = offered; *f != AV_PIX_FMT_NONE; f++) {
		if (*f == wanted)
			return *f;
	}
	// The GPU cannot decode this stream: take FFmpeg's software choice.
	return avcodec_default_get_format(context, offered);
}

} // namespace

void H264Decoder::Deleters::operator()(AVCodecContext *p) const
{
	avcodec_free_context(&p);
}
void H264Decoder::Deleters::operator()(AVFrame *p) const
{
	av_frame_free(&p);
}
void H264Decoder::Deleters::operator()(AVPacket *p) const
{
	av_packet_free(&p);
}
void H264Decoder::Deleters::operator()(AVBufferRef *p) const
{
	av_buffer_unref(&p);
}

H264Decoder::H264Decoder() = default;

H264Decoder::~H264Decoder() = default;

const char *H264Decoder::backend_name() const
{
	return is_hardware() ? "d3d11va" : "software";
}

bool H264Decoder::open(bool try_hardware)
{
	if (try_hardware && open_codec(true))
		return true;
	return open_codec(false);
}

void H264Decoder::close()
{
	context_.reset();
	hw_device_.reset();
	frame_.reset();
	sw_frame_.reset();
	packet_.reset();
	hw_pix_fmt_ = AV_PIX_FMT_NONE;
	hw_failures_ = 0;
	colors_logged_ = false;
}

bool H264Decoder::open_codec(bool use_hardware)
{
	close();

	const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	if (!codec) {
		error_ = "FFmpeg has no H.264 decoder";
		return false;
	}

	if (use_hardware) {
		for (int i = 0;; i++) {
			const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
			if (!config)
				break;
			if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
			    config->device_type == AV_HWDEVICE_TYPE_D3D11VA) {
				hw_pix_fmt_ = config->pix_fmt;
				break;
			}
		}

		AVBufferRef *device = nullptr;
		if (hw_pix_fmt_ == AV_PIX_FMT_NONE) {
			error_ = "FFmpeg build has no D3D11VA support for H.264";
			return false;
		}
		const int ret = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
		if (ret < 0) {
			error_ = "D3D11VA device: " + av_error_text(ret);
			return false;
		}
		hw_device_.reset(device);
	}

	context_.reset(avcodec_alloc_context3(codec));
	frame_.reset(av_frame_alloc());
	sw_frame_.reset(av_frame_alloc());
	packet_.reset(av_packet_alloc());
	if (!context_ || !frame_ || !sw_frame_ || !packet_) {
		error_ = "out of memory";
		close();
		return false;
	}

	// Latency choices: output each picture as soon as it is decoded (no reorder delay; the phone
	// encoder sends no B-frames), allow non-spec-compliant speedups, and never use frame
	// threading, which holds back one frame per thread.
	context_->flags |= AV_CODEC_FLAG_LOW_DELAY;
	context_->flags2 |= AV_CODEC_FLAG2_FAST;
	context_->thread_type = FF_THREAD_SLICE;
	context_->thread_count = use_hardware ? 1 : 0;

	if (use_hardware) {
		context_->hw_device_ctx = av_buffer_ref(hw_device_.get());
		context_->opaque = reinterpret_cast<void *>(static_cast<intptr_t>(hw_pix_fmt_));
		context_->get_format = pick_format;
	}

	const int ret = avcodec_open2(context_.get(), codec, nullptr);
	if (ret < 0) {
		error_ = std::string("avcodec_open2: ") + av_error_text(ret);
		close();
		return false;
	}
	return true;
}

H264Decoder::Result H264Decoder::decode(const uint8_t *data, size_t size, obs_source_frame &out)
{
	if (!context_)
		return Result::Error;

	// The payload stays owned by the caller; FFmpeg copies it if it needs to keep it.
	packet_->data = const_cast<uint8_t *>(data);
	packet_->size = static_cast<int>(size);

	int ret = avcodec_send_packet(context_.get(), packet_.get());
	if (ret == AVERROR(EAGAIN)) {
		// Only possible if an earlier frame was never pulled; drain it and retry once.
		avcodec_receive_frame(context_.get(), frame_.get());
		ret = avcodec_send_packet(context_.get(), packet_.get());
	}
	packet_->data = nullptr;
	packet_->size = 0;

	if (ret < 0) {
		error_ = "send_packet: " + av_error_text(ret);
		if (is_hardware() && ++hw_failures_ >= kMaxHardwareFailures) {
			error_ += " (falling back to software)";
			open_codec(false);
		}
		return Result::Error;
	}

	ret = avcodec_receive_frame(context_.get(), frame_.get());
	if (ret == AVERROR(EAGAIN))
		return Result::NoFrame;
	if (ret < 0) {
		error_ = "receive_frame: " + av_error_text(ret);
		return Result::Error;
	}
	hw_failures_ = 0;

	const AVFrame *picture = frame_.get();
	if (frame_->format == hw_pix_fmt_) {
		// GPU -> system memory as NV12. OBS uploads NV12 directly, so no swscale pass.
		av_frame_unref(sw_frame_.get());
		ret = av_hwframe_transfer_data(sw_frame_.get(), frame_.get(), 0);
		if (ret < 0) {
			error_ = "hwframe_transfer: " + av_error_text(ret);
			return Result::Error;
		}
		av_frame_copy_props(sw_frame_.get(), frame_.get());
		picture = sw_frame_.get();
	}

	return fill_obs_frame(picture, out) ? Result::Frame : Result::Error;
}

bool H264Decoder::fill_obs_frame(const AVFrame *frame, obs_source_frame &out)
{
	const video_format format = to_obs_format(frame->format);
	if (format == VIDEO_FORMAT_NONE) {
		error_ = std::string("unsupported pixel format ") + av_get_pix_fmt_name(AVPixelFormat(frame->format));
		return false;
	}

	for (size_t i = 0; i < MAX_AV_PLANES; i++) {
		out.data[i] = i < AV_NUM_DATA_POINTERS ? frame->data[i] : nullptr;
		out.linesize[i] = i < AV_NUM_DATA_POINTERS ? static_cast<uint32_t>(frame->linesize[i]) : 0;
	}
	out.width = static_cast<uint32_t>(frame->width);
	out.height = static_cast<uint32_t>(frame->height);
	out.format = format;
	out.flip = false;
	out.trc = VIDEO_TRC_DEFAULT;

	// Recomputed per frame: it is a few multiplications and the phone may change its VUI on a
	// new key frame.
	const video_colorspace colorspace = to_obs_colorspace(frame);
	const bool full_range = frame->color_range == AVCOL_RANGE_JPEG || frame->format == AV_PIX_FMT_YUVJ420P;
	out.full_range = full_range;
	video_format_get_parameters_for_format(colorspace, full_range ? VIDEO_RANGE_FULL : VIDEO_RANGE_PARTIAL,
					       format, out.color_matrix, out.color_range_min, out.color_range_max);

	if (!colors_logged_) {
		colors_logged_ = true;
		blog(LOG_INFO, "[phone-cam] decoded %dx%d %s, colorspace %s, %s range", frame->width,
		     frame->height, av_get_pix_fmt_name(AVPixelFormat(frame->format)),
		     av_color_space_name(frame->colorspace), full_range ? "full" : "limited");
	}
	return true;
}
