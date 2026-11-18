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

#include <atomic>
#include <cstdint>
#include <string>

// Runs `adb forward tcp:<port> tcp:<port>` so the phone's server is reachable on
// 127.0.0.1:<port> over the USB cable. adb is taken from PATH, else from the default Android
// SDK location. Returns false (with a reason) if adb is missing, fails, or `keep_running`
// turns false while waiting.
bool adb_forward(uint16_t port, const std::atomic<bool> &keep_running, std::string &error);
