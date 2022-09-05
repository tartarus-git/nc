#include "NetworkShepherd.h"

#include <cstdint>
#include <cstring>

#include <cerrno>

#include "error_reporting.h"

#ifndef PLATFORM_WINDOWS
#include <unistd.h>
#else
#include <io.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

using ssize_t = int;

#define read(...) _read(__VA_ARGS__)
#define write(...) _write(__VA_ARGS__)
#endif

#ifndef PLATFORM_WINDOWS
#include <sys/socket.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netdb.h>

using socket_t = int;

#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

#define GET_LAST_ERROR errno
#else
#include <winsock2.h>
#include <ws2tcpip.h>

using socket_t = SOCKET;

using socklen_t = int;

// TODO: Also, explore all the possible error codes for send and recv and all those calls, use the windows manpages
// for inspiration. Maybe you should do a couple real-world "stress-tests" to see which errors you actually need to handle specifically.

#define GET_LAST_ERROR WSAGetLastError()
#endif

#ifdef PLATFORM_WINDOWS
#pragma comment(lib, "Wsa2_32.lib")
#endif

#ifdef PLATFORM_WINDOWS
struct WSADATA NetworkShepherd::WSAData;
#endif

socket_t NetworkShepherd::listenerSocket;
socket_t NetworkShepherd::communicatorSocket;

// TODO: Along with whatever other TODO's are still spread out across the files, we still need to change this sa_family_t thing and make sure
// the headers and the ifdefs are correct everywhere. We also need to handle the UDP question and the backlog question.
sa_family_t NetworkShepherd::UDPSenderAddressFamily;

void NetworkShepherd::init() noexcept {
#ifdef PLATFORM_WINDOWS
	if (WSAStartup(MAKEWORD(2, 2), &WSAData) != 0) { REPORT_ERROR_AND_EXIT("WSAStartup failed", EXIT_FAILURE); }
#endif
}

/*
NOTE: About sockaddr and sockaddr_storage:
	- sockaddr has generic data bytes inside of it (14 I think) to accomodate things like sockaddr_in.
	- in that case, it works great, but with time, more complex sockaddr_x structures arose, and those didn't fit.
	- backwards-compatibility is annoying in this case, which is why the current system is suboptimal:
		- sockaddr_storage is what sockaddr should have been: It's guaranteed to have enough generic data bytes to
			be able to contain every possible sockaddr_x. We use sockaddr_storage in this program because it works
			with IPv6, since sockaddr_in6 doesn't fit inside sockaddr.
*/

#define CSA_RESOLVE_INTERFACES true
#define CSA_RESOLVE_HOSTNAMES false

template <bool resolve_interfaces_instead_of_hostnames>
struct sockaddr_storage construct_sockaddr(const char* node, uint16_t port, IPVersionConstraint nodeAddressIPVersionConstraint) noexcept {
#ifdef PLATFORM_WINDOWS
	static_assert(resolve_interfaces_instead_of_hostnames == CSA_RESOLVE_HOSTNAMES, "interface recognition not supported on Windows");
#endif

	struct addrinfo addressRetrievalHint;

#ifdef PLATFORM_WINDOWS
	addressRetrievalHint.ai_next = nullptr;
	addressRetrievalHint.ai_canonname = nullptr;
	addressRetrievalHint.ai_addr = nullptr;
	addressRetrievalHint.ai_addrlen = 0;
#endif

	addressRetrievalHint.ai_socktype = 0;
	addressRetrievalHint.ai_protocol = 0;
	addressRetrievalHint.ai_flags = 0;

#ifndef PLATFORM_WINDOWS
	if (resolve_interfaces_instead_of_hostnames) {
		struct ifaddrs* interfaceAddresses;

		if (getifaddrs(&interfaceAddresses) != -1) {

			for (struct ifaddrs* addr = interfaceAddresses; addr->ifa_next != nullptr; addr = addr->ifa_next) {
				if (std::strcmp(addr->ifa_name, node) == 0) {
					if (!addr->ifa_addr) { continue; }

					struct sockaddr_storage result_sockaddr;

					switch (nodeAddressIPVersionConstraint) {
					case IPVersionConstraint::NONE:
						if (addr->ifa_addr->sa_family == AF_INET6) { *(sockaddr_in6*)&result_sockaddr = *(sockaddr_in6*)addr->ifa_addr; break; }
					case IPVersionConstraint::FOUR:
						if (addr->ifa_addr->sa_family == AF_INET) { *(sockaddr_in*)&result_sockaddr = *(sockaddr_in*)addr->ifa_addr; break; }
						continue;
					case IPVersionConstraint::SIX:
						if (addr->ifa_addr->sa_family == AF_INET6) { *(sockaddr_in6*)&result_sockaddr = *(sockaddr_in6*)addr->ifa_addr; break; }
						continue;
					}

					((sockaddr_in*)&result_sockaddr)->sin_port = htons(port);

					freeifaddrs(interfaceAddresses);

					return result_sockaddr;
				}
			}

			freeifaddrs(interfaceAddresses);
		}

		addressRetrievalHint.ai_flags |= AI_NUMERICHOST;
	}
#endif

	switch (nodeAddressIPVersionConstraint) {
	case IPVersionConstraint::NONE: addressRetrievalHint.ai_family = AF_UNSPEC; break;
	case IPVersionConstraint::FOUR: addressRetrievalHint.ai_family = AF_INET; break;
	case IPVersionConstraint::SIX: addressRetrievalHint.ai_family = AF_INET6;
	}

	struct addrinfo* addressInfo;

	switch (getaddrinfo(node, nullptr, &addressRetrievalHint, &addressInfo)) {
		case 0: break;
		case EAI_AGAIN: REPORT_ERROR_AND_EXIT("temporary DNS lookup failure, try again later", EXIT_FAILURE);
		case EAI_FAIL: REPORT_ERROR_AND_EXIT("DNS lookup failed", EXIT_FAILURE);
		case EAI_MEMORY: REPORT_ERROR_AND_EXIT("sockaddr construction failed, out of memory", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case EAI_NODATA:
#else
		case WSANO_DATA:
#endif
			REPORT_ERROR_AND_EXIT("hostname does not possess any valid addresses", EXIT_FAILURE);

		case EAI_NONAME:
#ifndef PLATFORM_WINDOWS
			REPORT_ERROR_AND_EXIT("invalid address/hostname/interface", EXIT_FAILURE);
#else
			REPORT_ERROR_AND_EXIT("invalid address/hostname", EXIT_FAILURE);
#endif

#ifndef PLATFORM_WINDOWS
		case EAI_SYSTEM: REPORT_ERROR_AND_EXIT("sockaddr construction failed, system error", EXIT_FAILURE);
#endif

		default: REPORT_ERROR_AND_EXIT("sockaddr construction failed, unknown reason", EXIT_FAILURE);
	}

	for (struct addrinfo* info = addressInfo; info->ai_next != nullptr; info = info->ai_next) {
		struct sockaddr_storage result_sockaddr;

		switch (nodeAddressIPVersionConstraint) {
		case IPVersionConstraint::NONE:
			if (info->ai_addr->sa_family == AF_INET6) { *(sockaddr_in6*)&result_sockaddr = *(sockaddr_in6*)info->ai_addr; break; }
			if (info->ai_addr->sa_family == AF_INET) { *(sockaddr_in*)&result_sockaddr = *(sockaddr_in*)info->ai_addr; break; }
			continue;
		case IPVersionConstraint::FOUR: *(sockaddr_in*)&result_sockaddr = *(sockaddr_in*)info->ai_addr; break;
		case IPVersionConstraint::SIX: *(sockaddr_in6*)&result_sockaddr = *(sockaddr_in6*)info->ai_addr;
		}

		((sockaddr_in*)&result_sockaddr)->sin_port = htons(port);

		freeaddrinfo(addressInfo);

		return result_sockaddr;
	}
	REPORT_ERROR_AND_EXIT("either specified IP or IPs of specified hostname don't satisfy IP version constraint", EXIT_SUCCESS);
}

void NetworkShepherd::createListener(const char* address, uint16_t port, int socketType, IPVersionConstraint listenerIPVersionConstraint) noexcept {
#ifndef PLATFORM_WINDOWS
	struct sockaddr_storage listenerAddress = construct_sockaddr<CSA_RESOLVE_INTERFACES>(address, port, listenerIPVersionConstraint);
#else
	struct sockaddr_storage listenerAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(address, port, listenerIPVersionConstraint);
#endif

	listenerSocket = socket(listenerAddress.ss_family, socketType, 0);
	if (listenerSocket == INVALID_SOCKET) { REPORT_ERROR_AND_EXIT("failed to create TCP listener socket", EXIT_FAILURE); }

	switch (listenerIPVersionConstraint) {
	case IPVersionConstraint::NONE:
		if (listenerAddress.ss_family == AF_INET6) {
			int disabler = false;
			if (setsockopt(listenerSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&disabler, sizeof(disabler)) == SOCKET_ERROR) {
				REPORT_ERROR_AND_EXIT("failed to disable IPV6_V6ONLY on TCP listener with setsockopt", EXIT_FAILURE);
			}
		}
		break;
	case IPVersionConstraint::FOUR: /* do nothing */ break;
	case IPVersionConstraint::SIX:
		int enabler = true;
		if (setsockopt(listenerSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&enabler, sizeof(enabler)) == SOCKET_ERROR) {
			REPORT_ERROR_AND_EXIT("failed to enable IPV6_V6ONLY on TCP listener with setsockopt", EXIT_FAILURE);
		}
	}

	if (bind(listenerSocket, (const sockaddr*)&listenerAddress, sizeof(listenerAddress)) == SOCKET_ERROR) {
		switch (GET_LAST_ERROR) {
#ifndef PLATFORM_WINDOWS
		case EACCES:
#else
		case WSAEACCES:
#endif
			REPORT_ERROR_AND_EXIT("permission to bind TCP listener to address+port denied by local system", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case EADDRINUSE:
#else
		case WSAEADDRINUSE:
#endif
			if (port == 0) { REPORT_ERROR_AND_EXIT("bind TCP listener failed, no ephemeral ports available", EXIT_FAILURE); }
			REPORT_ERROR_AND_EXIT("bind TCP listener failed, port occupied", EXIT_FAILURE);

		default: REPORT_ERROR_AND_EXIT("bind TCP listener failed, unknown reason", EXIT_FAILURE);
		}
	}
}

// TODO: Solve that UDP problem with the linux OS. For some reason, UDP packets don't cross my network, even with the normal traditional netcat

void NetworkShepherd::listen(int backlogLength) noexcept {
	// TODO: Figure out this backlog thing for both OS's now.
	if (::listen(listenerSocket, backlogLength) == SOCKET_ERROR) {
		REPORT_ERROR_AND_EXIT("failed to listen with TCP listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd::accept() noexcept {
	communicatorSocket = ::accept(listenerSocket, nullptr, nullptr);
	if (communicatorSocket == INVALID_SOCKET) {
#ifndef PLATFORM_WINDOWS
		if (GET_LAST_ERROR == ECONNABORTED) {
#else
		if (GET_LAST_ERROR == WSAECONNRESET) {
#endif
			REPORT_ERROR_AND_EXIT("TCP listener accept connection failed, connection aborted", EXIT_FAILURE);
		}
		REPORT_ERROR_AND_EXIT("TCP listener accept connection failed, unknown reason", EXIT_FAILURE);
	}
}

void bindCommunicatorToSource(const char* sourceAddress_string, uint16_t sourcePort, IPVersionConstraint sourceAddressIPVersionConstraint) noexcept {
#ifndef PLATFORM_WINDOWS
	struct sockaddr_storage sourceAddress = construct_sockaddr<CSA_RESOLVE_INTERFACES>(sourceAddress_string, sourcePort, sourceAddressIPVersionConstraint);
#else
	struct sockaddr_storage sourceAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(sourceAddress_string, sourcePort, sourceAddressIPVersionConstraint);
#endif

// NOTE: Windows doesn't support IP_BIND_ADDRESS_NO_PORT, so it either does what I want it to without me having to tell it,
// or it binds the ephemeral port at the bind call, which isn't what I want but it's not bad, so I can live with that.
// (I'm leaning towards the former.)
// NOTE: Now that I think about it, having IP_BIND_ADDRESS_NO_PORT on Linux has no utility that I can see.
// It totally isn't harming us though and I have a feeling it's important for some edge-case so I'm going to leave it in.
#ifndef PLATFORM_WINDOWS
	int enabler = true;
	if (setsockopt(NetworkShepherd::communicatorSocket, IPPROTO_IP, IP_BIND_ADDRESS_NO_PORT, &enabler, sizeof(enabler)) == SOCKET_ERROR) {
		REPORT_ERROR_AND_EXIT("failed to enable IP_BIND_ADDRESS_NO_PORT on communicator with setsockopt", EXIT_FAILURE);
	}
#endif

	if (bind(NetworkShepherd::communicatorSocket, (const sockaddr*)&sourceAddress, sizeof(sourceAddress)) == -1) {
		switch (GET_LAST_ERROR) {
#ifndef PLATFORM_WINDOWS
		case EACCES:
#else
		case WSAEACCES:
#endif
			REPORT_ERROR_AND_EXIT("permission to bind communicator to source address+port denied by local system", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case EADDRINUSE:
#else
		case WSAEADDRINUSE:
#endif
			if (sourcePort == 0) {
				REPORT_ERROR_AND_EXIT("bind communicator failed, no ephemeral source ports available", EXIT_FAILURE);
			}
			REPORT_ERROR_AND_EXIT("bind communicator failed, source port occupied", EXIT_FAILURE);

		default: REPORT_ERROR_AND_EXIT("bind communicator failed, unknown reason", EXIT_FAILURE);
		}
	}
}

void NetworkShepherd::createCommunicatorAndConnect(const char* destinationAddress, uint16_t destinationPort, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint connectionIPVersionConstraint) noexcept {
	struct sockaddr_storage connectionTargetAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(destinationAddress, destinationPort, connectionIPVersionConstraint);
	
	communicatorSocket = socket(connectionTargetAddress.ss_family, SOCK_STREAM, 0);
	if (communicatorSocket == INVALID_SOCKET) { REPORT_ERROR_AND_EXIT("failed to construct TCP connection communicator socket", EXIT_FAILURE); }

	if (sourceAddress) {
		if (connectionIPVersionConstraint == IPVersionConstraint::NONE) {
			switch (connectionTargetAddress.ss_family) {
			case AF_INET: bindCommunicatorToSource(sourceAddress, sourcePort, IPVersionConstraint::FOUR); break;
			case AF_INET6: bindCommunicatorToSource(sourceAddress, sourcePort, IPVersionConstraint::SIX);
			}
		}
		else { bindCommunicatorToSource(sourceAddress, sourcePort, connectionIPVersionConstraint); }
	}

	if (connect(communicatorSocket, (const sockaddr*)&connectionTargetAddress, sizeof(connectionTargetAddress)) == SOCKET_ERROR) {
		switch (GET_LAST_ERROR) {
#ifndef PLATFORM_WINDOWS
		case EACCES: case EPERM:
#else
		case WSAEACCES:
#endif
			REPORT_ERROR_AND_EXIT("failed to connect, local system blocked attempt", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case EADDRNOTAVAIL: REPORT_ERROR_AND_EXIT("failed to connect, no ephemeral ports available", EXIT_FAILURE);
#else
		case WSAEADDRINUSE: REPORT_ERROR_AND_EXIT("failed to connect, source port occupied", EXIT_FAILURE);
#endif

#ifndef PLATFORM_WINDOWS
		case ECONNREFUSED:
#else
		case WSAECONNREFUSED:
#endif
			REPORT_ERROR_AND_EXIT("failed to connect, connection refused", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case ENETUNREACH:
#else
		case WSAENETUNREACH:
#endif
			REPORT_ERROR_AND_EXIT("failed to connect, network unreachable", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case ENETDOWN:
#else
		case WSAENETDOWN:
#endif
			REPORT_ERROR_AND_EXIT("failed to connect, network down", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case EHOSTUNREACH:
#else
		case WSAEHOSTUNREACH:
#endif
			REPORT_ERROR_AND_EXIT("failed to connect, host unreachable", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case ETIMEDOUT:
#else
		case WSAETIMEDOUT:
#endif
			REPORT_ERROR_AND_EXIT("failed to connect, connection attempt timed out", EXIT_FAILURE);

		default: REPORT_ERROR_AND_EXIT("failed to connect, unknown reason", EXIT_FAILURE);
		}
	}
}

size_t NetworkShepherd::read(void* buffer, size_t buffer_size) noexcept {
#ifndef PLATFORM_WINDOWS
	ssize_t bytesRead = ::read(communicatorSocket, buffer, buffer_size);
#else
	ssize_t bytesRead = recv(communicatorSocket, buffer, buffer_size, 0);
#endif
	if (bytesRead == SOCKET_ERROR) {
		switch (GET_LAST_ERROR) {
		// NOTE: ECONNRESET also happens when the connected network changes.
		// TODO: Actually, that might not be true, in the context of WSL, some things are kind of weird.
		// You need to run this on actual hardware and see if other things like network disconnect also trigger ECONNRESET.
		// Since a network change on the host doesn't actually affect the WSL instance, maybe Windows is sending ECONNRESET because of custom
		// configuration.
#ifndef PLATFORM_WINDOWS
		case ECONNRESET:
#else
		case WSAECONNRESET:
#endif
			REPORT_ERROR_AND_EXIT("failed to read from communicator socket, remote reset connection", EXIT_FAILURE);

		default: REPORT_ERROR_AND_EXIT("failed to read from communicator socket, unknown reason", EXIT_FAILURE);
		}
	}
	return bytesRead;
}

void NetworkShepherd::write(const void* buffer, size_t buffer_size) noexcept {
	while (true) {
#ifndef PLATFORM_WINDOWS
		// NOTE: MSG_NOSIGNAL means don't send SIGPIPE to our process when EPIPE situations are encountered, just return EPIPE without sending signal (usually does both).
		ssize_t bytesWritten = send(communicatorSocket, buffer, buffer_size, MSG_NOSIGNAL);
#else
		ssize_t bytesWritten = send(communicatorSocket, buffer, buffer_size, 0);
#endif
		if (bytesWritten == buffer_size) { return; }
		if (bytesWritten == SOCKET_ERROR) {
			switch (GET_LAST_ERROR) {
			// NOTE: ECONNRESET is for when remote resets before our send queue can be emptied.
			// NOTE: ECONNRESET also gets triggered when the connected network changes.	TODO: Expand on this as above.
			// NOTE: EPIPE is for when remote resets and our send queue is empty (last sent packet doesn't receive an ACK but still counts as sent).
#ifndef PLATFORM_WINDOWS
			case ECONNRESET: case EPIPE:
#else
			case WSAECONNRESET:
#endif
				REPORT_ERROR_AND_EXIT("failed to send on communicator socket, remote reset connection", EXIT_FAILURE);

			default: REPORT_ERROR_AND_EXIT("failed to send on communicator socket, unknown reason", EXIT_FAILURE);
			}
		}
		*(const char**)&buffer += bytesWritten;
		buffer_size -= bytesWritten;
	}
}

size_t NetworkShepherd::readUDP(void* buffer, size_t buffer_size) noexcept {
	ssize_t bytesRead = recv(listenerSocket, buffer, buffer_size, 0);		// NOTE: We use recv instead of read because read doesn't consume zero-length UDP packets and our program would hence get stuck if we used read.
	if (bytesRead == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("failed to recv from UDP listener socket, unknown reason", EXIT_FAILURE); }
	return bytesRead;
}

void NetworkShepherd::createUDPSender(const char* destinationAddress, uint16_t destinationPort, bool allowBroadcast, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint senderIPVersionConstraint) noexcept {
	struct sockaddr_storage targetAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(destinationAddress, destinationPort, senderIPVersionConstraint);
	UDPSenderAddressFamily = targetAddress.ss_family;		// TODO: Handle the type of this addr family stuff.

	communicatorSocket = socket(UDPSenderAddressFamily, SOCK_DGRAM, 0);
	if (communicatorSocket == INVALID_SOCKET) { REPORT_ERROR_AND_EXIT("failed to create UDP sender socket", EXIT_FAILURE); }

	if (allowBroadcast) {
		int enabler = true;
		if (setsockopt(communicatorSocket, SOL_SOCKET, SO_BROADCAST, (const char*)&enabler, sizeof(enabler)) == -1) {
			REPORT_ERROR_AND_EXIT("failed to allow broadcast on UDP sender socket with setsockopt", EXIT_FAILURE);
		}
	}

	if (sourceAddress) {
		if (senderIPVersionConstraint == IPVersionConstraint::NONE) {
			switch (UDPSenderAddressFamily) {
			case AF_INET: bindCommunicatorToSource(sourceAddress, sourcePort, IPVersionConstraint::FOUR); break;
			case AF_INET6: bindCommunicatorToSource(sourceAddress, sourcePort, IPVersionConstraint::SIX);
			}
		}
		else { bindCommunicatorToSource(sourceAddress, sourcePort, senderIPVersionConstraint); }
	}

	// TODO: error handling here.
	if (connect(communicatorSocket, (const sockaddr*)&targetAddress, sizeof(targetAddress)) == SOCKET_ERROR) {
		switch (GET_LAST_ERROR) {
		case EACCES: case EPERM: REPORT_ERROR_AND_EXIT("failed to connect, local system blocked attempt", EXIT_FAILURE);
		case EADDRNOTAVAIL: REPORT_ERROR_AND_EXIT("failed to connect, no ephemeral ports available", EXIT_FAILURE);
		// NOTE: These shouldn't happen for UDP.
		//case ECONNREFUSED: REPORT_ERROR_AND_EXIT("failed to connect, connection refused", EXIT_FAILURE);
		//case ENETUNREACH: REPORT_ERROR_AND_EXIT("failed to connect, network unreachable", EXIT_FAILURE);
		//case ENETDOWN: REPORT_ERROR_AND_EXIT("failed to connect, network down", EXIT_FAILURE);
		//case EHOSTUNREACH: REPORT_ERROR_AND_EXIT("failed to connect, host unreachable", EXIT_FAILURE);
		//case ETIMEDOUT: REPORT_ERROR_AND_EXIT("failed to connect, connection attempt timed out", EXIT_FAILURE);
		default: REPORT_ERROR_AND_EXIT("failed to connect, unknown reason", EXIT_FAILURE);
		}
	}
}

void NetworkShepherd::writeUDP(const void* buffer, uint16_t buffer_size) noexcept {
	while (true) {
#ifndef PLATFORM_WINDOWS
		ssize_t bytesSent = ::write(communicatorSocket, buffer, buffer_size);
#else
		ssize_t bytesSent = send(communicatorSocket, buffer, buffer_size, 0);
#endif
		if (bytesSent == buffer_size) { return; }
		if (bytesSent == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("failed to write to UDP sender socket", EXIT_FAILURE); }
		*(const char**)&buffer += bytesSent;
		buffer_size -= bytesSent;
	}
}

uint16_t NetworkShepherd::getMSSApproximation() noexcept {
	int MTU;
	socklen_t MTU_buffer_size = sizeof(MTU);
	if (getsockopt(communicatorSocket, IPPROTO_IP, IP_MTU, (const char*)&MTU, &MTU_buffer_size) == -1) {
		REPORT_ERROR_AND_EXIT("failed to get MTU from UDP sender socket with getsockopt", EXIT_FAILURE);
	}
	if (UDPSenderAddressFamily == AF_INET) { return MTU - 20 - 8; }
	return MTU - 40 - 8;
}

void NetworkShepherd::enableFindMSS() noexcept {
	int doMTUDiscovery = IP_PMTUDISC_DO;
	if (setsockopt(communicatorSocket, IPPROTO_IP, IP_MTU_DISCOVER, (const char*)&doMTUDiscovery, sizeof(doMTUDiscovery)) == -1) {
		REPORT_ERROR_AND_EXIT("failed to enable MTU discovery on UDP sender socket with setsockopt", EXIT_FAILURE);
	}
}

uint16_t NetworkShepherd::writeUDPAndFindMSS(const void* buffer, uint16_t buffer_size) noexcept {
	uint16_t buffer_chunk_size = buffer_size;
	const char* buffer_end = *(const char**)&buffer + buffer_size;
	uint16_t result = 0;
	while (true) {
#ifndef PLATFORM_WINDOWS
		ssize_t bytesSent = ::write(communicatorSocket, buffer, buffer_chunk_size);
#else
		ssize_t bytesSent = send(communicatorSocket, buffer, buffer_chunk_size, 0);
#endif
		if (bytesSent == buffer_chunk_size) { return result; }
		if (bytesSent == SOCKET_ERROR) {
			switch (errno) {
#ifndef PLATFORM_WINDOWS
			case EMSGSIZE:
#else
			case WSAEMSGSIZE:
#endif
				buffer_chunk_size = getMSSApproximation();
				result = buffer_chunk_size;
				continue;

			default: REPORT_ERROR_AND_EXIT("failed to write to UDP sender socket", EXIT_FAILURE);
			}
		}
		*(const char**)&buffer += bytesSent;
		buffer_size = buffer_end - *(const char**)&buffer;
		if (buffer_size < buffer_chunk_size) { buffer_chunk_size = buffer_size; }
	}
	return result;
}

void NetworkShepherd::shutdownCommunicatorWrite() noexcept {
#ifdef PLATFORM_WINDOWS
#pragma push_macro("SHUT_WR")
#define SHUT_WR SD_SEND
#endif
	if (shutdown(communicatorSocket, SHUT_WR) == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("failed to shutdown communicator socket write", EXIT_FAILURE); }
#ifdef PLATFORM_WINDOWS
#pragma pop_macro("SHUT_WR")
#endif
}

void NetworkShepherd::closeCommunicator() noexcept {
#ifndef PLATFORM_WINDOWS
	int result = close(communicatorSocket);
#else
	int result = closesocket(communicatorSocket);
#endif
	if (result == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("failed to close communicator socket", EXIT_FAILURE); }
}

void NetworkShepherd::closeListener() noexcept {
#ifndef PLATFORM_WINDOWS
	int result = close(listenerSocket);
#else
	int result = closesocket(listenerSocket);
#endif
	if (result == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("failed to close listener socket", EXIT_FAILURE); }
}

void NetworkShepherd::release() noexcept {
#ifdef PLATFORM_WINDOWS
	if (WSACleanup() == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("WSACleanup failed"); }
#endif
}
