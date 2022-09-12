#pragma once

#include <cstdlib>

#ifndef PLATFORM_WINDOWS

#include <unistd.h>

#define crossplatform_write(...) write(__VA_ARGS__)

#else

#include <io.h>

#define STDERR_FILENO 2

#define crossplatform_write(...) _write(__VA_ARGS__)

#endif

#define MAX_DIGITS_IN_SIGNED_INT32 11

template <size_t message_length>
void writeErrorAndExit(const char (&message)[message_length], int exitCode) noexcept {
	crossplatform_write(STDERR_FILENO, message, message_length - 1);
	std::exit(exitCode);
}

#define REPORT_ERROR_AND_EXIT(message, exitCode) writeErrorAndExit("ERROR: " message "\n", exitCode)

#define crossplatform_write_literal(fd, literal) crossplatform_write(fd, literal, sizeof(literal) - 1)

template <size_t message_length>
void writeErrorAndCodeAndExit(const char (&message)[message_length], int errorCode, int exitCode) noexcept {
	crossplatform_write(STDERR_FILENO, message, message_length - 1);

	if (errorCode == 0) {
		crossplatform_write_literal(STDERR_FILENO, " (platform-dependant error code: 0)\n");
		std::exit(exitCode);
	}

	unsigned char digits[MAX_DIGITS_IN_SIGNED_INT32];
	unsigned char* digits_ptr = digits + MAX_DIGITS_IN_SIGNED_INT32;

	bool errorCode_negative = errorCode < 0;

	while (errorCode != 0) {
		int new_errorCode = errorCode / 10;
		if (errorCode_negative) { errorCode = new_errorCode * 10 - errorCode; }
		else { errorCode -= new_errorCode * 10; }
		*(--digits_ptr) = errorCode + '0';
		errorCode = new_errorCode;
	}
	if (errorCode_negative) { *(--digits_ptr) = '-'; }

	crossplatform_write_literal(STDERR_FILENO, " (platform-dependant error code: ");
	crossplatform_write(STDERR_FILENO, digits_ptr, digits + MAX_DIGITS_IN_SIGNED_INT32 - digits_ptr);
	crossplatform_write_literal(STDERR_FILENO, ")\n");

	std::exit(exitCode);
}

#define REPORT_ERROR_AND_CODE_AND_EXIT(message, errorCode, exitCode) writeErrorAndCodeAndExit("ERROR: " message, errorCode, exitCode)
