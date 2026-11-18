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

#include "adb-forward.hpp"

#include <windows.h>

#include <vector>

namespace {

// The first adb call may start the adb server, which takes a few seconds.
constexpr DWORD kAdbTimeoutMs = 10000;
constexpr DWORD kPollMs = 100;

std::wstring find_adb()
{
	wchar_t path[MAX_PATH];
	if (SearchPathW(nullptr, L"adb.exe", nullptr, MAX_PATH, path, nullptr) > 0)
		return path;

	DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
	if (size == 0)
		return {};
	std::vector<wchar_t> local_app_data(size);
	GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data.data(), size);

	std::wstring sdk_adb = std::wstring(local_app_data.data()) + L"\\Android\\Sdk\\platform-tools\\adb.exe";
	if (GetFileAttributesW(sdk_adb.c_str()) != INVALID_FILE_ATTRIBUTES)
		return sdk_adb;
	return {};
}

} // namespace

bool adb_forward(uint16_t port, const std::atomic<bool> &keep_running, std::string &error)
{
	const std::wstring adb = find_adb();
	if (adb.empty()) {
		error = "adb.exe not found (PATH or %LOCALAPPDATA%\\Android\\Sdk\\platform-tools)";
		return false;
	}

	const std::wstring tcp = L"tcp:" + std::to_wstring(port);
	std::wstring command_line = L"\"" + adb + L"\" forward " + tcp + L" " + tcp;

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION process = {};
	// CREATE_NO_WINDOW: no console flashing over OBS every time we retry.
	if (!CreateProcessW(adb.c_str(), command_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
			    nullptr, &startup, &process)) {
		error = "cannot start adb (error " + std::to_string(GetLastError()) + ")";
		return false;
	}
	CloseHandle(process.hThread);

	DWORD waited = 0;
	DWORD wait_result = WAIT_TIMEOUT;
	while (keep_running && waited < kAdbTimeoutMs) {
		wait_result = WaitForSingleObject(process.hProcess, kPollMs);
		if (wait_result != WAIT_TIMEOUT)
			break;
		waited += kPollMs;
	}

	DWORD exit_code = 1;
	if (wait_result == WAIT_OBJECT_0) {
		GetExitCodeProcess(process.hProcess, &exit_code);
	} else {
		TerminateProcess(process.hProcess, 1);
		error = keep_running ? "adb timed out" : "stopped";
	}
	CloseHandle(process.hProcess);

	if (wait_result == WAIT_OBJECT_0 && exit_code != 0)
		error = "adb forward failed (exit " + std::to_string(exit_code) +
			"): phone not plugged in or not authorized?";
	return wait_result == WAIT_OBJECT_0 && exit_code == 0;
}
