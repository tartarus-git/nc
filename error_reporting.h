#pragma once

#ifndef PLATFORM_WINDOWS
#include <unistd.h>
#else
#include <io.h>
#endif
#include <cstdlib>

#ifdef PLATFORM_WINDOWS

#define STDERR_FILENO 2

#define write(fd, buf, buflen) _write(fd, buf, buflen);

#endif

template <size_t message_length>
void writeErrorAndExit(const char (&message)[message_length], int exitCode) noexcept {
	write(STDERR_FILENO, message, message_length);
	std::exit(exitCode);
}

#define REPORT_ERROR_AND_EXIT(message, exitCode) writeErrorAndExit("ERROR: " message "\n", exitCode)
