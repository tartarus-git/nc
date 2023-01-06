#pragma once

#ifndef PLATFORM_WINDOWS

#include <unistd.h>

using iosize_t = size_t;
using sioret_t = ssize_t;

inline sioret_t crossplatform_read(int fd, void* buf, iosize_t count) noexcept { return ::read(fd, buf, count); }
inline sioret_t crossplatform_write(int fd, const void* buf, iosize_t count) noexcept { return ::write(fd, buf, count); }

#else

#include <type_traits>

#include <io.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

using iosize_t = int;
using sioret_t = int;

inline sioret_t crossplatform_read(int fd, void* buf, iosize_t count) noexcept { return ::_read(fd, buf, count); }
inline sioret_t crossplatform_write(int fd, const void* buf, iosize_t count) noexcept { return ::_write(fd, buf, count); }

#endif

inline sioret_t crossplatform_read_entire_buffer(int fd, void* buffer, iosize_t size) noexcept {
	char* byte_buffer = (char*)buffer;
	const iosize_t original_size = size;

	while (true) {
		sioret_t bytes_read = crossplatform_read(fd, byte_buffer, size);
		if (bytes_read == 0) { return original_size - size; }
		if (bytes_read == -1) { return -1; }
		size -= bytes_read;
		if (size == 0) { return original_size; }
		byte_buffer += bytes_read;
	}
}

inline bool crossplatform_write_entire_buffer(int fd, const void* buffer, iosize_t size) noexcept {
	const char* byte_buffer = (const char*)buffer;

	while (true) {
		sioret_t bytes_written = crossplatform_write(fd, byte_buffer, size);
		if (bytes_written == -1) { return false; }
		size -= bytes_written;
		if (size == 0) { return true; }
		byte_buffer += bytes_written;
	}
}
