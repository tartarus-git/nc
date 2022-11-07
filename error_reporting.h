#pragma once

#include <cstdlib>

#include "crossplatform_io.h"

#include "halt_program.h"

#define MAX_DIGITS_IN_SIGNED_INT32 11

#define static_strlen(string) (sizeof(string) - 1)
#define crossplatform_write_entire_literal(fd, literal) crossplatform_write_entire_buffer(fd, literal, static_strlen(literal))

template <size_t message_length>
void writeErrorAndExit(const char (&message)[message_length], int exitCode) noexcept {
	crossplatform_write_entire_literal(STDERR_FILENO, message);
	halt_program(exitCode);
}

#define REPORT_ERROR_AND_EXIT(message, exitCode) writeErrorAndExit("ERROR: " message "\n", exitCode)

template <iosize_t message_length>
void writeErrorAndCodeAndExit(const char (&message)[message_length], int errorCode, int exitCode) noexcept {
	crossplatform_write_entire_literal(STDERR_FILENO, message);

	char digits[MAX_DIGITS_IN_SIGNED_INT32];
	char* digits_ptr = digits + MAX_DIGITS_IN_SIGNED_INT32;

	bool errorCode_negative = errorCode < 0;
	unsigned long long positive_errorCode = (long long)errorCode * (-1 * errorCode_negative);
	// NOTE: Casting to bigger type first to avoid signed overflow when negating it, which could happen otherwise.
	// NOTE: SIGNED OVERFLOW IS UNDEFINED BEHAVIOR, WE AVOID IT AT ALL COSTS!

	// NOTE: Unroll one loop to make sure the case where errorCode is 0 is handled.
	char digit = positive_errorCode % 10;
	*(--digits_ptr) = digit + '0';
	positive_errorCode /= 10;

	while (positive_errorCode != 0) {
		char digit = positive_errorCode % 10;
		*(--digits_ptr) = digit + '0';
		positive_errorCode /= 10;
	}

	if (errorCode_negative) { *(--digits_ptr) = '-'; }

	crossplatform_write(STDERR_FILENO, digits_ptr, digits + MAX_DIGITS_IN_SIGNED_INT32 - digits_ptr);
	crossplatform_write_entire_literal(STDERR_FILENO, ")\n");

	halt_program(exitCode);
}

#define REPORT_ERROR_AND_CODE_AND_EXIT(message, errorCode, exitCode) writeErrorAndCodeAndExit("ERROR: " message " (platform-dependant error code: ", errorCode, exitCode)
