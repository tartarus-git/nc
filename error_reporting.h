#pragma once

#include <cstdlib>

#ifndef PLATFORM_WINDOWS
#include <unistd.h>
#else
#include <io.h>

#define STDERR_FILENO 2
#define write(...) _write(__VA_ARGS__);
#endif

template <size_t message_length>
void writeErrorAndExit(const char (&message)[message_length], int exitCode) noexcept {
	write(STDERR_FILENO, message, message_length);
	std::exit(exitCode);
}

#define REPORT_ERROR_AND_EXIT(message, exitCode) writeErrorAndExit("ERROR: " message "\n", exitCode)
