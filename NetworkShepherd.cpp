#include "NetworkShepherd.h"

#include <cstdint>		// for fixed-width stuff
#include <cstring>		// for std::strcmp

#include "crossplatform_io.h"

#include "error_retrieval.h"

#ifndef PLATFORM_WINDOWS

#include <sys/socket.h>		// for Linux sockets
#include <sys/types.h>		// for Linux system types
#include <ifaddrs.h>		// for getifaddrs function and supporting struct
#include <netdb.h>		// for getaddrinfo (I think), because that does DNS requests (hence a sort of "network database")

using socket_t = int;
using sockaddr_storage_family_t = sa_family_t;

#define INVALID_SOCKET -1
#define SOCKET_ERROR -1

#define GET_LAST_ERROR get_last_error()

#else

#include <winsock2.h>		// for Windows sockets
#include <ws2tcpip.h>		// for extentions to Windows sockets relating to TCP/IP

using socket_t = SOCKET;
using socklen_t = int;
using sockaddr_storage_family_t = short;

#define GET_LAST_ERROR WSAGetLastError()

#endif

#include "error_reporting.h"

#ifdef PLATFORM_WINDOWS
#pragma comment(lib, "Ws2_32.lib")	// statically link with winsock2 lib, because Windows is annoying
#endif

#ifdef PLATFORM_WINDOWS

WSADATA NetworkShepherd::WSAData;

#endif

socket_t NetworkShepherd::listenerSocket;
socket_t NetworkShepherd::communicatorSocket;

sockaddr_storage_family_t NetworkShepherd::UDPSenderAddressFamily;

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

#ifdef PLATFORM_WINDOWS		// NOTE: Windows is pedantic about these fields being zeroed out.
	addressRetrievalHint.ai_next = nullptr;
	addressRetrievalHint.ai_canonname = nullptr;
	addressRetrievalHint.ai_addr = nullptr;
	addressRetrievalHint.ai_addrlen = 0;
#endif

	addressRetrievalHint.ai_socktype = 0;
	addressRetrievalHint.ai_protocol = 0;
	addressRetrievalHint.ai_flags = 0;

// NOTE: Interface names aren't practical to type in the terminal in Windows, so presumably not a lot of people would use this functionality
// if we had it. That isn't the actual reason though, the actual reason is that this interface thing blocks the use of the localhost
// hostname, which people would probably much rather use than typing in some longwinded interface name.
// NOTE: This is different on Linux, since one can easily just type "lo" if one requires the loopback interface.
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
		// NOTE: Interestingly, these error codes are the same in Linux and Windows, so no #ifdef's required.
		case EAI_AGAIN: REPORT_ERROR_AND_EXIT("sockaddr construction failed, temporary DNS lookup failure, try again later", EXIT_FAILURE);
		case EAI_FAIL: REPORT_ERROR_AND_EXIT("sockaddr construction failed, DNS lookup failed", EXIT_FAILURE);
		case EAI_MEMORY: REPORT_ERROR_AND_EXIT("sockaddr construction failed, out of memory", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case EAI_NODATA:
#else
		case WSANO_DATA:
#endif
			REPORT_ERROR_AND_EXIT("sockaddr construction failed, hostname does not possess any valid addresses", EXIT_FAILURE);

		case EAI_NONAME:
#ifndef PLATFORM_WINDOWS
			REPORT_ERROR_AND_EXIT("sockaddr construction failed, invalid address/hostname/interface", EXIT_FAILURE);
#else
			REPORT_ERROR_AND_EXIT("sockaddr construction failed, invalid address/hostname", EXIT_FAILURE);
#endif

#ifndef PLATFORM_WINDOWS
		case EAI_SYSTEM: REPORT_ERROR_AND_EXIT("sockaddr construction failed, system error", EXIT_FAILURE);
#endif

		default: REPORT_ERROR_AND_EXIT("sockaddr construction failed, unknown reason", EXIT_FAILURE);
	}

	for (struct addrinfo* info = addressInfo; ; info = info->ai_next) {
		struct sockaddr_storage result_sockaddr;

		switch (nodeAddressIPVersionConstraint) {
		case IPVersionConstraint::NONE:
			if (info->ai_addr->sa_family == AF_INET6) { *(sockaddr_in6*)&result_sockaddr = *(sockaddr_in6*)info->ai_addr; break; }
			if (info->ai_addr->sa_family == AF_INET) { *(sockaddr_in*)&result_sockaddr = *(sockaddr_in*)info->ai_addr; break; }

			if (!info->ai_next) { break; }
			continue;
		case IPVersionConstraint::FOUR: *(sockaddr_in*)&result_sockaddr = *(sockaddr_in*)info->ai_addr; break;
		case IPVersionConstraint::SIX: *(sockaddr_in6*)&result_sockaddr = *(sockaddr_in6*)info->ai_addr;
		}

		((sockaddr_in*)&result_sockaddr)->sin_port = htons(port);

		freeaddrinfo(addressInfo);

		return result_sockaddr;
	}
	// NOTE: The following code should only be able to be reached in the Linux version of the program, since it captures non-IP addresses as well.
	REPORT_ERROR_AND_EXIT("sockaddr construction failed, hostname does not possess any IP addresses", EXIT_FAILURE);
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
		int error = GET_LAST_ERROR;
		switch (error) {
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

		default: REPORT_ERROR_AND_CODE_AND_EXIT("bind TCP listener failed, unknown reason", error, EXIT_FAILURE);
		}
	}
}

void NetworkShepherd::listen(int backlogLength) noexcept {
	if (::listen(listenerSocket, backlogLength) == SOCKET_ERROR) {
		REPORT_ERROR_AND_EXIT("failed to listen with TCP listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd::accept() noexcept {
	communicatorSocket = ::accept(listenerSocket, nullptr, nullptr);
	if (communicatorSocket == INVALID_SOCKET) {
		int error = GET_LAST_ERROR;
#ifndef PLATFORM_WINDOWS
		if (error == ECONNABORTED) {
#else
		if (error == WSAECONNRESET) {
#endif
			REPORT_ERROR_AND_EXIT("TCP listener accept connection failed, connection aborted", EXIT_FAILURE);
		}

		REPORT_ERROR_AND_CODE_AND_EXIT("TCP listener accept connection failed, unknown reason", error, EXIT_FAILURE);
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
// (I'm leaning towards the latter)
// NOTE: Now that I think about it, having IP_BIND_ADDRESS_NO_PORT on Linux has no utility that I can see.
// It totally isn't harming us though and I have a feeling it's important for some edge-case so I'm going to leave it in.
#ifndef PLATFORM_WINDOWS
	int enabler = true;
	if (setsockopt(NetworkShepherd::communicatorSocket, IPPROTO_IP, IP_BIND_ADDRESS_NO_PORT, &enabler, sizeof(enabler)) == SOCKET_ERROR) {
		REPORT_ERROR_AND_EXIT("failed to enable IP_BIND_ADDRESS_NO_PORT on communicator with setsockopt", EXIT_FAILURE);
	}
#endif

	if (bind(NetworkShepherd::communicatorSocket, (const sockaddr*)&sourceAddress, sizeof(sourceAddress)) == -1) {
		int error = GET_LAST_ERROR;
		switch (error) {
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
				// NOTE: This error shouldn't technically happen on Linux because of the above setsockopt, but I'm gonna
				// leave this in just in case. I have to leave it in for Windows anyway since it can happen there.
				REPORT_ERROR_AND_EXIT("bind communicator failed, no ephemeral source ports available", EXIT_FAILURE);
			}
			REPORT_ERROR_AND_EXIT("bind communicator failed, source port occupied", EXIT_FAILURE);

		default: REPORT_ERROR_AND_CODE_AND_EXIT("bind communicator failed, unknown reason", error, EXIT_FAILURE);
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
		int error = GET_LAST_ERROR;
		switch (error) {
#ifndef PLATFORM_WINDOWS
		case EACCES: case EPERM:
#else
		case WSAEACCES:
#endif
			REPORT_ERROR_AND_EXIT("failed to connect, local system blocked attempt", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case EADDRNOTAVAIL: REPORT_ERROR_AND_EXIT("failed to connect, no ephemeral ports available", EXIT_FAILURE);
#else
		// NOTE: Strangely, the following does not exist for Linux, even though it would be useful.
		// NOTE: Turns out, you can connect to a wild-card address on Linux, it just connects to any listening address on the
		// local machine AFAIK. How it decides which listening address to connect to is beyond me.
		case WSAEADDRNOTAVAIL: REPORT_ERROR_AND_EXIT("failed to connect, target IP address invalid", EXIT_FAILURE);

		// NOTE: It doesn't seem like Windows delays the selection of ephemeral ports until the connect syscall like Linux can.
		// It seems to select a port and bind to it at the bind call. The only exception to this is when using wild-card
		// addresses (0.0.0.0 and co.). I don't know if it makes up it's mind on the bind call about which port to connect to or not,
		// but I know that it does the actual binding at the connect syscall (even if the port is explicitly specified).
		// If that port isn't available on at least one of the IP's is binds to, this error will get thrown AFAIK.
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

		default: REPORT_ERROR_AND_CODE_AND_EXIT("failed to connect, unknown reason", error, EXIT_FAILURE);
		}
	}
}

// NOTE: This functions return value will never ever ever be less than
// 0, because errors are handled inside the function and error
// reporting and program termination is done there as well.
sioret_t NetworkShepherd::read(void* buffer, iosize_t buffer_size) noexcept {
#ifndef PLATFORM_WINDOWS
	sioret_t bytesRead = ::read(communicatorSocket, buffer, buffer_size);
#else
	sioret_t bytesRead = recv(communicatorSocket, (char*)buffer, buffer_size, 0);
#endif

	if (bytesRead == SOCKET_ERROR) {
		int error = GET_LAST_ERROR;
		switch (error) {
			// NOTE: Super interesting: It seems if you disconnect the WiFi (probs works for Ethernet as well) and then connect it
			// again without waiting too long, the OS keeps your connections alive, which is super cool.
			// It's only when you CHANGE the WiFi connection or the connection times out that you get either ECONNRESET/ABORTED
			// or ETIMEDOUT. I assume that you'll get the ECONNRESET/ABORTED thing even with the same WiFi if your IP address
			// happens to change when coming back to the network.

#ifndef PLATFORM_WINDOWS
		case ECONNRESET:
#else
		case WSAECONNRESET:
		case WSAECONNABORTED:	// NOTE: When local system terminates connection (e.g. network change). Linux uses ECONNRESET for this AFAIK.
#endif
			REPORT_ERROR_AND_EXIT("failed to read from communicator socket, connection reset", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
		case ETIMEDOUT:
#else
		case WSAETIMEDOUT:
			REPORT_ERROR_AND_EXIT("failed to read from communicator socket, connection timed out", EXIT_FAILURE);
#endif

		// NOT-SO-FUN-FACT: MSVC treats macro invocations with too many args as warnings instead of errors (they should be errors
		// as per the standard). This is so fucking stupid, I feel like MSVC is bad for me.
		// TODO: Switch out all your Windows compiler stuff to clang.
		default: REPORT_ERROR_AND_CODE_AND_EXIT("failed to read from communicator socket, unknown reason", error, EXIT_FAILURE);
		}
	}

	return bytesRead;
}

// NOTE: It's a bit strange but this function always writes the whole buffer, it doesn't return prematurely. It's only weird because it's
// different from the write syscall, but it works nicely for this program. There's no reason not to do it like this.
void NetworkShepherd::write(const void* buffer, iosize_t buffer_size) noexcept {
	const char* byte_buffer = (const char*)buffer;

	while (true) {
#ifndef PLATFORM_WINDOWS
		// NOTE: MSG_NOSIGNAL means don't send SIGPIPE to our process when EPIPE situations are encountered, just return EPIPE without sending signal (usually does both).
		sioret_t bytesWritten = send(communicatorSocket, byte_buffer, buffer_size, MSG_NOSIGNAL);
#else
		sioret_t bytesWritten = send(communicatorSocket, byte_buffer, buffer_size, 0);
#endif
		if (bytesWritten == buffer_size) { return; }

		if (bytesWritten == SOCKET_ERROR) {
			int error = GET_LAST_ERROR;
			switch (error) {
			// NOTE: ECONNRESET is for when remote resets before our send queue can be emptied.
			// NOTE: ECONNRESET also gets triggered when the connected network changes (see read function above).
			// NOTE: EPIPE is for when remote resets and our send queue is empty (last sent packet doesn't receive an ACK but still counts as sent).

#ifndef PLATFORM_WINDOWS
			case ECONNRESET:
			case EPIPE:
#else
			case WSAECONNRESET:
			case WSAECONNABORTED:
#endif
				REPORT_ERROR_AND_EXIT("failed to send on communicator socket, connection reset", EXIT_FAILURE);

#ifndef PLATFORM_WINDOWS
			case ETIMEDOUT:
#else
			case WSAETIMEDOUT:
				REPORT_ERROR_AND_EXIT("failed to send on communicator socket, connection timed out", EXIT_FAILURE);
#endif

			default: REPORT_ERROR_AND_CODE_AND_EXIT("failed to send on communicator socket, unknown reason", error, EXIT_FAILURE);
			}
		}

		/*
		   It's time I wrote about this because it's important, I've been avoiding it because it's kind of complicated.
		   It's often called type punning or type aliasing, basically it's accessing the same data through pointers of different types.
		   One example of this is getting two pointers as inputs in a function:
		   	- If the pointers point to similar (that word is very concretely defined in the standard) types,
				it is assumed that the pointers can point to the same data/the arrays can overlap or something.
			- that means that the compiler has to write to actual RAM for way more variable accesses, to make sure
				that the second pointer can read the correct data when it is written through the first pointer.
				(basically, it prevents a fair bit of optimizations)
			- this whole similar thing was done to at least offer some room for optimizations:
				- if the input pointers don't point to similar types then the compiler can assume that their
					data areas do not overlap, since that's how it normally is in real life.
			- god knows why the C++ people didn't just implement the restrict keyword like C did, would have been a great
				solution to the function thing. I've heard it's because it would be too difficult and weird to implement with C++ templates and the C++ type system, but the idea is good. TODO: Find out exactly why something similar to restrict hasn't been implemented.

		   So you can see why type punning through reinterpret_cast or *(x*)&y is a problem:

		   	- You're constructing a pointer of another type that refers to the same data as another pointer.
			- Access through that pointer is super duper UB because the compiler assumes that these two pointers do not refer to the same data.
			- As a result: your instructions could be optimized in such a way that you don't read the expected data
				when you read from the same spot in memory. THIS IS SUPER DANGEROUS!

		   Everywhere where I say similar, this is what I (and the standard) mean:
			- if two types are the same, they are similar
		   	- if the pointed-to-type is similar between two pointers, the pointers are similar.
			- also, if the pointers are pointers to members, they have to be of the same class.
			- theres another one thats not super important.
			(also, top level cv-qualifiers are ignored)

			--> it's complexly expressed, but I think this essentially means:
				- the types are similar if they are the same, barring any cv-inequalities on any level of the type.

		    --> ALSO, in addition to being similar, type punning is also allowed in every case where the other type
		    	is the signed/unsigned counter-part to the original type.
			It is ALSO allowed when the alias pointer, as in the pointer type that you're using to access the data that was already set
			through another pointer, is a pointer to char or unsigned char or signed char.
			--> the char pointer thing is very important. It breaks the undefined behaviour just long enough for you to
			gather insight into the representation of any type, which is necessary in the day-to-day.

		------> ALL IN ALL: you should avoid type punning unless your alias type is a char or unsigned char or signed char
		------>			or the types are similar or the signed/unsigned thing from above.

			// NOTE: MAKE SURE TO BE CAREFUL WITH TYPE PUNNING. One wrong move and you're undefined in an instant.

			// ALSO IMPORTANT: This kind of behavior (assuming the pointers don't alias and all that) is called strict aliasing and
			// it applies more generally than what I've said above. It's got to do with the underlying type of the memory
			// and with the lifetime of objects in C++, but I'm not going to get into that here. I've got other comments
			// in other repos that explain it a lot better.
		*/

		// NOTE: The following is still defined behavior though AFAIK,
		// because you can cast to whatever pointer you want,
		// it's simply the use that can cause UB.
		// Here, we're still accessing the buffer bytes
		// through char alias, which is well defined.
		//*(const char**)&buffer += bytesWritten;
		// We obviously could have also written: (const char*)buffer += bytesWritten;

		byte_buffer += bytesWritten;
		buffer_size -= bytesWritten;
	}
}

sioret_t NetworkShepherd::readUDP(void* buffer, iosize_t buffer_size) noexcept {
	sioret_t bytesRead = recv(listenerSocket, (char*)buffer, buffer_size, 0);		// NOTE: We use recv instead of read because read doesn't consume zero-length UDP packets and our program would hence get stuck if we used read.
	if (bytesRead == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("failed to recv from UDP listener socket, unknown reason", EXIT_FAILURE); }
	return bytesRead;
}

void NetworkShepherd::createUDPSender(const char* destinationAddress, uint16_t destinationPort, bool allowBroadcast, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint senderIPVersionConstraint) noexcept {
	struct sockaddr_storage targetAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(destinationAddress, destinationPort, senderIPVersionConstraint);
	UDPSenderAddressFamily = targetAddress.ss_family;

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

	if (connect(communicatorSocket, (const sockaddr*)&targetAddress, sizeof(targetAddress)) == SOCKET_ERROR) {
		int error = GET_LAST_ERROR;
		switch (error) {
#ifndef PLATFORM_WINDOWS
		// NOTE: No need to make a Windows version for this one apparently, since Windows doesn't have an equivalent.
		case EACCES: case EPERM: REPORT_ERROR_AND_EXIT("failed to connect, local system blocked attempt", EXIT_FAILURE);
#endif

#ifndef PLATFORM_WINDOWS
		case EADDRNOTAVAIL: REPORT_ERROR_AND_EXIT("failed to connect, no ephemeral ports available", EXIT_FAILURE);
#else
		case WSAEADDRNOTAVAIL: REPORT_ERROR_AND_EXIT("failed to connect, target IP address invalid", EXIT_FAILURE);
		case WSAEADDRINUSE: REPORT_ERROR_AND_EXIT("failed to connect, source port occupied", EXIT_FAILURE);
#endif

		// NOTE: These shouldn't happen for UDP.
		//case ECONNREFUSED: REPORT_ERROR_AND_EXIT("failed to connect, connection refused", EXIT_FAILURE);
		//case ENETUNREACH: REPORT_ERROR_AND_EXIT("failed to connect, network unreachable", EXIT_FAILURE);
		//case ENETDOWN: REPORT_ERROR_AND_EXIT("failed to connect, network down", EXIT_FAILURE);
		//case EHOSTUNREACH: REPORT_ERROR_AND_EXIT("failed to connect, host unreachable", EXIT_FAILURE);
		//case ETIMEDOUT: REPORT_ERROR_AND_EXIT("failed to connect, connection attempt timed out", EXIT_FAILURE);
		default: REPORT_ERROR_AND_CODE_AND_EXIT("failed to connect, unknown reason", error, EXIT_FAILURE);
		}
	}
}

void NetworkShepherd::writeUDP(const void* buffer, uint16_t buffer_size) noexcept {
	while (true) {
#ifndef PLATFORM_WINDOWS
		sioret_t bytesSent = ::write(communicatorSocket, buffer, buffer_size);
#else
		sioret_t bytesSent = send(communicatorSocket, (char*)buffer, buffer_size, 0);
#endif
		if (bytesSent == buffer_size) { return; }
		if (bytesSent == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("failed to write to UDP sender socket", EXIT_FAILURE); }
		*(const char**)&buffer += bytesSent;
		buffer_size -= bytesSent;
	}
}

// NOTE: The following two functions (just like all the other ones (that I can think of)) can only be called after communicatorSocket is created.

uint16_t NetworkShepherd::getMSSApproximation() noexcept {
#ifndef PLATFORM_WINDOWS
	int MTU;
#else
	DWORD MTU;
#endif
	socklen_t MTU_buffer_size = sizeof(MTU);

	int level;
	int optname;
	if (UDPSenderAddressFamily == AF_INET6) {
		level = IPPROTO_IPV6;
		optname = IPV6_MTU;
	} else {
		level = IPPROTO_IP;
		optname = IP_MTU;
	}

	if (getsockopt(communicatorSocket, level, optname, (char*)&MTU, &MTU_buffer_size) == SOCKET_ERROR) {
		REPORT_ERROR_AND_EXIT("failed to get MTU from UDP sender socket with getsockopt", EXIT_FAILURE);
	}

	return UDPSenderAddressFamily == AF_INET6 ? MTU - 40 - 8 : MTU - 20 - 8;
}

void NetworkShepherd::enableFindMSS() noexcept {
#ifndef PLATFORM_WINDOWS
	int doMTUDiscovery = IP_PMTUDISC_DO;
#else
	DWORD doMTUDiscovery = IP_PMTUDISC_DO;
#endif

	int level;
	int optname;
	if (UDPSenderAddressFamily == AF_INET6) {
		level = IPPROTO_IPV6;
		optname = IPV6_MTU_DISCOVER;
	} else {
		level = IPPROTO_IP;
		optname = IP_MTU_DISCOVER;
	}

	if (setsockopt(communicatorSocket, level, optname, (const char*)&doMTUDiscovery, sizeof(doMTUDiscovery)) == SOCKET_ERROR) {
		REPORT_ERROR_AND_EXIT("failed to enable MTU discovery on UDP sender socket with setsockopt", EXIT_FAILURE);
	}
}

uint16_t NetworkShepherd::writeUDPAndFindMSS(const void* buffer, uint16_t buffer_size) noexcept {
	uint16_t buffer_chunk_size = buffer_size;
	const char* buffer_end = *(const char**)&buffer + buffer_size;
	uint16_t result = 0;
	while (true) {
#ifndef PLATFORM_WINDOWS
		sioret_t bytesSent = ::write(communicatorSocket, buffer, buffer_chunk_size);
#else
		sioret_t bytesSent = send(communicatorSocket, (char*)buffer, buffer_chunk_size, 0);
#endif
		if (bytesSent == buffer_chunk_size) { return result; }
		if (bytesSent == SOCKET_ERROR) {
			int error = GET_LAST_ERROR;
			switch (error) {
#ifndef PLATFORM_WINDOWS
			case EMSGSIZE:
#else
			case WSAEMSGSIZE:
#endif
				buffer_chunk_size = getMSSApproximation();
				result = buffer_chunk_size;
				continue;

			default: REPORT_ERROR_AND_CODE_AND_EXIT("failed to write to UDP sender socket", error, EXIT_FAILURE);
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
	if (WSACleanup() == SOCKET_ERROR) { REPORT_ERROR_AND_EXIT("WSACleanup failed", EXIT_FAILURE); }
#endif
}
