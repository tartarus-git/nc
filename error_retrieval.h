#pragma once

#ifndef PLATFORM_WINDOWS

#include <cerrno>

using last_error_t = decltype(errno);

inline last_error_t get_last_error() noexcept { return errno; }

#else

#include <Windows.h>	// NOTE: Can't do WIN32_LEAN_AND_MEAN here because that disables functionality that other #includes of Windows.h might need.
			// NOTE: Even if we push and pop and make sure it isn't defined everywhere, the include guard in Windows.h probably
			// isn't smart enough to add the missing functionality when included again.

using last_error_t = DWORD;

inline last_error_t get_last_error() noexcept { return GetLastError(); }

#endif
