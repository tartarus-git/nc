#pragma once

#include <sys/socket.h>

#include <cstdint>

enum class IPVersionConstraint : uint8_t {
	NONE,
	FOUR,
	SIX
};

class NetworkShepherd {
	int listenerSocket;
	int communicatorSocket;

	struct sockaddr UDPSenderTargetAddress;

	void init() noexcept;

	void createListener(const char* address, uint16_t port, int socketType, IPVersionConstraint listenerIPVersionConstraint) noexcept;

	void listen(int backlogLength) noexcept;
	void accept() noexcept;

	void createCommunicatorAndConnect(const char* destinationAddress, uint16_t destinationPort, const char* sourceAddress) noexcept;

	size_t read(void* buffer, size_t buffer_size) noexcept;
	void write(const void* buffer, size_t buffer_size) noexcept;

	size_t readUDP(void* buffer, size_t buffer_size) noexcept;

	void createUDPSender(const char* destinationAddress, uint16_t destinationPort, IPVersionConstraint destinationIPVersionConstraint, bool allowBroadcast, const char* sourceAddress) noexcept;

	void sendUDP(const void* buffer, size_t buffer_size) noexcept;

	void closeCommunicator() noexcept;

	void closeListener() noexcept;

	void release() noexcept;
};
