#pragma once

#include <sys/socket.h>

#include <cstdint>

// NOTE: I guess you could get rid of this and use AF_UNSPEC, AF_INET and AF_INET6 throughout the code.
// NOTE: That would be more efficient in some places, but I think in the grand scheme of things,
// the current way might even be better since we can use it in switch cases without the compiler
// generating the typical default if checking boiler-plate. Although that could be avoided even with AF_*
// if the compiler is smart enough, but I don't know if it is in this case. TODO: We would have
// to change everything to AF_* and look at the assembly to see if the produced code is just as good.
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
