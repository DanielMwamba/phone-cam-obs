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

#include <cstdint>
#include <string>

// Pairing code for PCAM v2 (docs/protocol.md): the PC proves it knows the code shown on the
// phone without sending it.
namespace pcam {

constexpr size_t kPairingCodeLength = 6;

// Keeps the digits of what the user typed ("482 913" -> "482913"). Returns false unless that
// leaves exactly 6 digits.
bool normalize_pairing_code(const std::string &typed, std::string &code);

// HMAC-SHA256(key = code, message = "PCAM-PAIR" || nonce).
void pairing_response(const std::string &code, const uint8_t (&nonce)[kNonceSize],
		      uint8_t (&response)[kPairingResponseSize]);

} // namespace pcam
