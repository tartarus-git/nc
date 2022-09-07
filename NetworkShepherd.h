#pragma once

#include <cstdint>

#ifndef PLATFORM_WINDOWS
#include <sys/socket.h>

using socket_t = int;
using sockaddr_storage_family_t = sa_family_t;
#else
#include <winsock2.h>

using socket_t = SOCKET;
using sockaddr_storage_family_t = short;
#endif

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
#ifdef PLATFORM_WINDOWS
	static struct WSADATA WSAData;
#endif

public:
	static socket_t listenerSocket;
	static socket_t communicatorSocket;

	static sockaddr_storage_family_t UDPSenderAddressFamily;

	static void init() noexcept;

	static void createListener(const char* address, uint16_t port, int socketType, IPVersionConstraint listenerIPVersionConstraint) noexcept;

	static void listen(int backlogLength) noexcept;
	static void accept() noexcept;

	static void createCommunicatorAndConnect(const char* destinationAddress, uint16_t destinationPort, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint connectionIPVersionConstraint) noexcept;

	static size_t read(void* buffer, size_t buffer_size) noexcept;
	static void write(const void* buffer, size_t buffer_size) noexcept;

	static size_t readUDP(void* buffer, size_t buffer_size) noexcept;

	static void createUDPSender(const char* destinationAddress, uint16_t destinationPort, bool allowBroadcast, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint senderIPVersionConstraint) noexcept;

	static void writeUDP(const void* buffer, uint16_t buffer_size) noexcept;

	static uint16_t getMSSApproximation() noexcept;

	static void enableFindMSS() noexcept;

	static uint16_t writeUDPAndFindMSS(const void* buffer, uint16_t buffer_size) noexcept;

	static void shutdownCommunicatorWrite() noexcept;

	static void closeCommunicator() noexcept;

	static void closeListener() noexcept;

	static void release() noexcept;
};
