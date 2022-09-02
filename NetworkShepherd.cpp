#include "NetworkShepherd.h"

#include <cstdint>
#include <cstring>

#include <sys/socket.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netdb.h>

#include <unistd.h>

#include <cerrno>

#include "error_reporting.h"

#define CSA_RESOLVE_INTERFACES true
#define CSA_RESOLVE_HOSTNAMES false

int NetworkShepherd::listenerSocket;
int NetworkShepherd::communicatorSocket;

struct sockaddr NetworkShepherd::UDPSenderTargetAddress;

template <bool resolve_interfaces_instead_of_hostnames>
sockaddr construct_sockaddr(const char* node, uint16_t port, IPVersionConstraint nodeAddressIPVersionConstraint) noexcept {
	struct addrinfo addressRetrievalHint;
	addressRetrievalHint.ai_family = AF_UNSPEC;
	addressRetrievalHint.ai_socktype = 0;
	addressRetrievalHint.ai_protocol = 0;
	addressRetrievalHint.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;

	sa_family_t addressFamilyConstraint;
	switch (nodeAddressIPVersionConstraint) {
	case IPVersionConstraint::NONE: addressFamilyConstraint = AF_UNSPEC; break;
	case IPVersionConstraint::FOUR: addressFamilyConstraint = AF_INET; break;
	case IPVersionConstraint::SIX: addressFamilyConstraint = AF_INET6;
	}

	if (resolve_interfaces_instead_of_hostnames) {
		struct ifaddrs* interfaceAddresses;

		if (getifaddrs(&interfaceAddresses) != -1) {

			for (struct ifaddrs* addr = interfaceAddresses; addr->ifa_next != nullptr; addr = addr->ifa_next) {
				if (std::strcmp(addr->ifa_name, node) == 0) {
					if (!addr->ifa_addr) { continue; }

					// TODO: Fix this broken logic somehow.
					if (addressFamilyConstraint != AF_UNSPEC && addr->ifa_addr->sa_family != addressFamilyConstraint) { continue; }

					struct sockaddr result_sockaddr = *(addr->ifa_addr);			// TODO: Actually understand how sockaddr's work.
					((sockaddr_in*)&result_sockaddr)->sin_port = htons(port);

					freeifaddrs(interfaceAddresses);

					return result_sockaddr;
				}
			}

			freeifaddrs(interfaceAddresses);
		}

		addressRetrievalHint.ai_flags |= AI_NUMERICHOST;
	}

	struct addrinfo* addressInfo;

	switch (getaddrinfo(node, nullptr, &addressRetrievalHint, &addressInfo)) {
		case 0: break;
		case EAI_AGAIN: REPORT_ERROR_AND_EXIT("temporary DNS lookup failure, try again later", EXIT_SUCCESS);
		case EAI_FAIL: REPORT_ERROR_AND_EXIT("DNS lookup failed", EXIT_SUCCESS);
		case EAI_MEMORY: REPORT_ERROR_AND_EXIT("sockaddr construction failed, out of memory", EXIT_FAILURE);
		case EAI_NODATA: REPORT_ERROR_AND_EXIT("hostname does not possess any addresses", EXIT_SUCCESS);
		case EAI_NONAME: REPORT_ERROR_AND_EXIT("invalid address/hostname/interface", EXIT_SUCCESS);
		case EAI_SYSTEM: REPORT_ERROR_AND_EXIT("sockaddr construction failed, system error", EXIT_FAILURE);
		default: REPORT_ERROR_AND_EXIT("sockaddr construction failed, unknown reason", EXIT_FAILURE);
		// TODO: What you really should be doing is letting the OS translate error codes in the default case, just so the user can
		// still get some information.
	}

	for (struct addrinfo* info = addressInfo; info->ai_next != nullptr; info = info->ai_next) {
		if (addressFamilyConstraint != AF_UNSPEC && info->ai_addr->sa_family != addressFamilyConstraint) { continue; }

		struct sockaddr result_sockaddr = *(info->ai_addr);
		((sockaddr_in*)&result_sockaddr)->sin_port = htons(port);

		freeaddrinfo(addressInfo);

		return result_sockaddr;
	}
	REPORT_ERROR_AND_EXIT("either specified IP or IPs of specified hostname don't satisfy IP version constraint", EXIT_SUCCESS);
}

void NetworkShepherd::init() noexcept { }

void NetworkShepherd::createListener(const char* address, uint16_t port, int socketType, IPVersionConstraint listenerIPVersionConstraint) noexcept {
	struct sockaddr listenerAddress = construct_sockaddr<CSA_RESOLVE_INTERFACES>(address, port, listenerIPVersionConstraint);

	listenerSocket = socket(listenerAddress.sa_family, socketType, 0);
	if (listenerSocket == -1) { REPORT_ERROR_AND_EXIT("failed to create listener socket", EXIT_FAILURE); }

	switch (listenerIPVersionConstraint) {
	case IPVersionConstraint::NONE:
		if (listenerAddress.sa_family == AF_INET6) {
			int disabler = false;
			if (setsockopt(listenerSocket, IPPROTO_IPV6, IPV6_V6ONLY, &disabler, sizeof(disabler)) == -1) {
				REPORT_ERROR_AND_EXIT("failed to disable IPV6_V6ONLY with setsockopt", EXIT_FAILURE);
			}
		}
		break;
	case IPVersionConstraint::FOUR: /* do nothing */ break;
	case IPVersionConstraint::SIX:
		int enabler = true;
		if (setsockopt(listenerSocket, IPPROTO_IPV6, IPV6_V6ONLY, &enabler, sizeof(enabler)) == -1) {
			REPORT_ERROR_AND_EXIT("failed to enable IPV6_V6ONLY with setsockopt", EXIT_FAILURE);
		}
	}

	if (bind(listenerSocket, &listenerAddress, sizeof(listenerAddress)) == -1) {
		REPORT_ERROR_AND_EXIT("failed to bind listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd::listen(int backlogLength) noexcept {
	if (::listen(listenerSocket, backlogLength) == -1) {
		REPORT_ERROR_AND_EXIT("failed to listen with listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd::accept() noexcept {
	communicatorSocket = ::accept(listenerSocket, nullptr, nullptr);
	if (communicatorSocket == -1) {
		if (errno == ECONNABORTED) {
			REPORT_ERROR_AND_EXIT("failed to accept connection because it was aborted", EXIT_SUCCESS);
		}
		REPORT_ERROR_AND_EXIT("failed to accept connection on listener socket", EXIT_FAILURE);
	}
}

void bindCommunicatorToSource(const char* sourceAddress_string, uint16_t sourcePort, IPVersionConstraint sourceAddressIPVersionConstraint) noexcept {
	struct sockaddr sourceAddress = construct_sockaddr<CSA_RESOLVE_INTERFACES>(sourceAddress_string, sourcePort, sourceAddressIPVersionConstraint);

	int enabler = true;
	if (setsockopt(NetworkShepherd::communicatorSocket, IPPROTO_IP, IP_BIND_ADDRESS_NO_PORT, &enabler, sizeof(enabler)) == -1) {
		REPORT_ERROR_AND_EXIT("failed to enable IP_BIND_ADDRESS_NO_PORT with setsockopt", EXIT_FAILURE);
	}

	if (bind(NetworkShepherd::communicatorSocket, &sourceAddress, sizeof(sourceAddress)) == -1) {
		switch (errno) {
		case EACCES:
			REPORT_ERROR_AND_EXIT("permission to bind socket to source address/port denied by local system", EXIT_SUCCESS);
		default:
			REPORT_ERROR_AND_EXIT("bind socket to source address/port failed, unknown reason", EXIT_FAILURE);
		}
	}
}

void NetworkShepherd::createCommunicatorAndConnect(const char* destinationAddress, uint16_t destinationPort, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint connectionIPVersionConstraint) noexcept {
	struct sockaddr connectionTargetAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(destinationAddress, destinationPort, connectionIPVersionConstraint);
	
	communicatorSocket = socket(connectionTargetAddress.sa_family, SOCK_STREAM, 0);
	if (communicatorSocket == -1) { REPORT_ERROR_AND_EXIT("failed to construct connection communicator socket", EXIT_FAILURE); }

	if (sourceAddress) {
		if (connectionIPVersionConstraint == IPVersionConstraint::NONE) {
			switch (connectionTargetAddress.sa_family) {
			case AF_INET: bindCommunicatorToSource(sourceAddress, sourcePort, IPVersionConstraint::FOUR); break;
			case AF_INET6: bindCommunicatorToSource(sourceAddress, sourcePort, IPVersionConstraint::SIX);
			}
		}
		else { bindCommunicatorToSource(sourceAddress, sourcePort, connectionIPVersionConstraint); }
	}

	if (connect(communicatorSocket, &connectionTargetAddress, sizeof(connectionTargetAddress)) == -1) {
		switch (errno) {
			case EACCES: case EPERM:
				REPORT_ERROR_AND_EXIT("failed to connect, local system blocked attempt", EXIT_FAILURE);
			case EADDRNOTAVAIL:
				REPORT_ERROR_AND_EXIT("failed to connect, no ephemeral ports available", EXIT_FAILURE);
			case ECONNREFUSED:
				REPORT_ERROR_AND_EXIT("failed to connect, connection refused", EXIT_FAILURE);
			case ENETUNREACH:
				REPORT_ERROR_AND_EXIT("failed to connect, network unreachable", EXIT_FAILURE);
			case ENETDOWN:
				REPORT_ERROR_AND_EXIT("failed to connect, network down", EXIT_FAILURE);
			case EHOSTUNREACH:
				REPORT_ERROR_AND_EXIT("failed to connect, host unreachable", EXIT_FAILURE);
			case ETIMEDOUT:
				REPORT_ERROR_AND_EXIT("failed to connect, connection attempt timed out", EXIT_FAILURE);
			default:
				REPORT_ERROR_AND_EXIT("failed to connect, unknown reason", EXIT_FAILURE);
		}
	}
}

size_t NetworkShepherd::read(void* buffer, size_t buffer_size) noexcept {
	ssize_t bytesRead = ::read(communicatorSocket, buffer, buffer_size);
	if (bytesRead == -1) { REPORT_ERROR_AND_EXIT("failed to read from communicator socket", EXIT_FAILURE); }
	return bytesRead;
}

void NetworkShepherd::write(const void* buffer, size_t buffer_size) noexcept {
	while (true) {
		// NOTE: MSG_NOSIGNAL means don't send SIGPIPE to our process when EPIPE situations are encountered, just return EPIPE without sending signal (usually does both).
		ssize_t bytesWritten = send(communicatorSocket, buffer, buffer_size, MSG_NOSIGNAL);
		if (bytesWritten == buffer_size) { return; }
		if (bytesWritten == -1) {
			switch (errno) {
			// NOTE: ECONNRESET is for when remote resets before our send queue can be emptied.
			// NOTE: EPIPE is for when remote resets and our send queue is empty (last sent packet doesn't receive an ACK but still counts as sent).
			case ECONNRESET: case EPIPE: REPORT_ERROR_AND_EXIT("failed to send to communicator socket, remote reset connection", EXIT_FAILURE);
			default: REPORT_ERROR_AND_EXIT("failed to send to communicator socket, unknown reason", EXIT_FAILURE);
			}
		}
		*(const char**)&buffer += bytesWritten;
		buffer_size -= bytesWritten;
	}
}

size_t NetworkShepherd::readUDP(void* buffer, size_t buffer_size) noexcept {
	ssize_t bytesRead = recv(listenerSocket, buffer, buffer_size, 0);		// NOTE: We use recv instead of read because read doesn't consume zero-length UDP packets and our program would hence get stuck if we used read.
	if (bytesRead == -1) { REPORT_ERROR_AND_EXIT("failed to recv from listener socket, unknown reason", EXIT_FAILURE); }
	return bytesRead;
}

void NetworkShepherd::createUDPSender(const char* destinationAddress, uint16_t destinationPort, bool allowBroadcast, const char* sourceAddress, uint16_t sourcePort, IPVersionConstraint senderIPVersionConstraint) noexcept {
	UDPSenderTargetAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(destinationAddress, destinationPort, senderIPVersionConstraint);

	communicatorSocket = socket(UDPSenderTargetAddress.sa_family, SOCK_DGRAM, 0);
	if (communicatorSocket == -1) { REPORT_ERROR_AND_EXIT("failed to create UDP sender communicator socket", EXIT_FAILURE); }

	if (allowBroadcast) {
		int enabler = true;
		if (setsockopt(communicatorSocket, SOL_SOCKET, SO_BROADCAST, &enabler, sizeof(enabler)) == -1) {
			REPORT_ERROR_AND_EXIT("failed to allow broadcast on UDP sender socket with setsockopt", EXIT_FAILURE);
		}
	}

	if (sourceAddress) {
		if (senderIPVersionConstraint == IPVersionConstraint::NONE) {
			switch (UDPSenderTargetAddress.sa_family) {
			case AF_INET: bindCommunicatorToSource(sourceAddress, sourcePort, IPVersionConstraint::FOUR); break;
			case AF_INET6: bindCommunicatorToSource(sourceAddress, sourcePort, IPVersionConstraint::SIX);
			}
		}
		else { bindCommunicatorToSource(sourceAddress, sourcePort, senderIPVersionConstraint); }
	}
}

void NetworkShepherd::writeUDP(const void* buffer, size_t buffer_size) noexcept {
	while (true) {
		ssize_t bytesSent = sendto(communicatorSocket, buffer, buffer_size, 0, &UDPSenderTargetAddress, sizeof(UDPSenderTargetAddress));
		if (bytesSent == buffer_size) { return; }
		if (bytesSent == -1) { REPORT_ERROR_AND_EXIT("failed to sendto on UDP sender communicator socket", EXIT_FAILURE); }
		*(const char**)&buffer += bytesSent;
		buffer_size -= bytesSent;
	}
}

void NetworkShepherd::shutdownCommunicatorWrite() noexcept {
	if (shutdown(communicatorSocket, SHUT_WR) == -1) { REPORT_ERROR_AND_EXIT("failed to shutdown communicator socket write", EXIT_FAILURE); }
}

void NetworkShepherd::closeCommunicator() noexcept {
	if (close(communicatorSocket) == -1) { REPORT_ERROR_AND_EXIT("failed to close communicator socket", EXIT_FAILURE); }
}

void NetworkShepherd::closeListener() noexcept {
	if (close(listenerSocket) == -1) {
		REPORT_ERROR_AND_EXIT("failed to close listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd::release() noexcept { }
