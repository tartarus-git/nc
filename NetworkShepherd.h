#pragma once

#include <sys/socket.h>

#include <cstdint>

// TODO: I guess you could get rid of this and use AF_UNSPEC, AF_INET and AF_INET6 throughout the code, would be a small bit more efficient.
enum class IPVersionConstraint : uint8_t {
	NONE,
	FOUR,
	SIX
};

class NetworkShepherd {
public:
	static int listenerSocket;
	static int communicatorSocket;

	static struct sockaddr_storage UDPSenderTargetAddress;

	static void init() noexcept;

	static void createListener(const char* address, uint16_t port, int socketType, IPVersionConstraint listenerIPVersionConstraint) noexcept;

	static void listen(int backlogLength) noexcept;
	static void accept() noexcept;

	static void createCommunicatorAndConnect(const char* destinationAddress, uint16_t destinationPort, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint connectionIPVersionConstraint) noexcept;

	static size_t read(void* buffer, size_t buffer_size) noexcept;
	static void write(const void* buffer, size_t buffer_size) noexcept;

	static size_t readUDP(void* buffer, size_t buffer_size) noexcept;

	static void createUDPSender(const char* destinationAddress, uint16_t destinationPort, bool allowBroadcast, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint senderIPVersionConstraint) noexcept;

	static void writeUDP(const void* buffer, size_t buffer_size) noexcept;

	static void shutdownCommunicatorWrite() noexcept;

	static void closeCommunicator() noexcept;

	static void closeListener() noexcept;

	static void release() noexcept;
};
