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

#include "pcam-auth.hpp"

extern "C" {
#include <libavutil/hmac.h>
}

#include <cctype>
#include <cstring>

namespace pcam {

namespace {

constexpr char kContext[] = "PCAM-PAIR";

} // namespace

bool normalize_pairing_code(const std::string &typed, std::string &code)
{
	code.clear();
	for (const char c : typed) {
		if (std::isdigit(static_cast<unsigned char>(c)))
			code.push_back(c);
		else if (!std::isspace(static_cast<unsigned char>(c)))
			return false;
	}
	return code.size() == kPairingCodeLength;
}

void pairing_response(const std::string &code, const uint8_t (&nonce)[kNonceSize],
		      uint8_t (&response)[kPairingResponseSize])
{
	// FFmpeg's HMAC: OBS already ships avutil, so no crypto library to add.
	AVHMAC *hmac = av_hmac_alloc(AV_HMAC_SHA256);
	av_hmac_init(hmac, reinterpret_cast<const uint8_t *>(code.data()), static_cast<unsigned int>(code.size()));
	av_hmac_update(hmac, reinterpret_cast<const uint8_t *>(kContext), sizeof(kContext) - 1);
	av_hmac_update(hmac, nonce, kNonceSize);
	av_hmac_final(hmac, response, kPairingResponseSize);
	av_hmac_free(hmac);
}

} // namespace pcam
