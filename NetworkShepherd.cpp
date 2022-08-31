#include "NetworkShepherd.h"
// TODO: collect all the necessary headers here and in NetworkShepherd.h

#include "error_reporting.h"

#define CSA_RESOLVE_INTERFACES true
#define CSA_RESOLVE_HOSTNAMES false

template <bool resolve_interfaces_instead_of_hostnames>
sockaddr construct_sockaddr(const char* node, uint16_t port) noexcept {
	struct addrinfo addressRetrievalHint;
	addressRetrievalHint.ai_family = AF_UNSPEC;
	addressRetrievalHint.ai_socktype = 0;
	addressRetrievalHint.ai_protocol = 0;
	addressRetrievalHint.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;

	if (resolve_interfaces_instead_of_hostnames) {
		struct ifaddrs* interfaceAddresses;

		if (getifaddrs(&interfaceAddresses) != -1) {

			for (struct ifaddrs* addr = interfaceAddresses; addr->ifa_next != nullptr; addr = info->ifa_next) {
				if (std::strcmp(addr->ifa_name, node) == 0) {
					struct result_sockaddr = addr->ifa_addr;
					result_sockaddr.port = htons(port);

					freeifaddrs(interfaceAddresses);

					return result_sockaddr;
				}
			}

			freeifaddrs(interfaceAddresses);
		}

		addressRetrievalHint.ai_flags |= AI_NUMERICHOST;
	}

	struct addrinfo* addressInfo;

	switch (getaddrinfo(node, nullptr, nullptr, &addressInfo)) {
		case EAI_AGAIN: REPORT_ERROR_AND_EXIT("temporary DNS lookup failure, try again later", EXIT_SUCCESS);
		case EAI_FAIL: REPORT_ERROR_AND_EXIT("DNS lookup failed", EXIT_SUCCESS);
		case EAI_MEMORY: REPORT_ERROR_AND_EXIT("sockaddr construction failed, out of memory", EXIT_FAILURE);
		case EAI_NODATA: REPORT_ERROR_AND_EXIT("hostname does not possess any addresses", EXIT_SUCCESS);
		case EAI_NONAME: REPORT_ERROR_AND_EXIT("invalid address/hostname", EXIT_SUCCESS);
		case EAI_SYSTEM: REPORT_ERROR_AND_EXIT("sockaddr construction failed, system error", EXIT_FAILURE);
	}

	for (struct addrinfo* info = addressInfo; info->ai_next != nullptr; info = info->ai_next) {
		if (info->ai_family == AF_INET6 || info->ai_family == AF_INET) {
			struct sockaddr result_sockaddr = info->ai_addr;
			result_sockaddr.port = htons(port);

			freeaddrinfo(addressInfo);

			return result_sockaddr;
		}
	}
	REPORT_ERROR_AND_EXIT("hostname does not posess any IP addresses", EXIT_SUCCESS);
}

void NetworkShepherd::init() noexcept { }

void NetworkShepherd::createListener(const char* address, uint16_t port, int socketType, IPVersionConstraint listenerIPVersionConstraint) noexcept {
	struct sockaddr listenerAddress = construct_sockaddr<CSA_RESOLVE_INTERFACES>(address, port);

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
	case IPVersionConstraint::FOUR:
		if (listenerAddress.sa_family == AF_INET6) {
			REPORT_ERROR_AND_EXIT("\"-4\" flag invalid with IPv6 address", EXIT_SUCCESS);
		}
		break;
	case IPVersionConstraint::SIX:
		if (listenerAddress.sa_family == AF_INET) {
			REPORT_ERROR_AND_EXIT("\"-6\" flag invalid with IPv4 address", EXIT_SUCCESS);
		}
		int enabler = true;
		if (setsockopt(listenerSocket, IPPROTO_IPV6, IPV6_V6ONLY, &enabler, sizeof(enabler)) == -1) {
			REPORT_ERROR_AND_EXIT("failed to enable IPV6_V6ONLY with setsockopt", EXIT_FAILURE);
		}
	}

	if (bind(listenerSocket, &listenerAddress, sizeof(listenerAddress) == -1) {
		REPORT_ERROR_AND_EXIT("failed to bind listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd::listen(int backlogLength) noexcept {
	if (listen(listenerSocket, backLogLength) == -1) {
		REPORT_ERROR_AND_EXIT("failed to listen with listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd::accept() noexcept {
	connectionSocket = accept(listenerSocket, nullptr, nullptr);
	if (connectionSocket == -1) {
		if (errno == ECONNABORTED) {
			REPORT_ERROR_AND_EXIT("failed to accept connection because it was aborted", EXIT_SUCCESS);
		}
		REPORT_ERROR_AND_EXIT("failed to accept connection on listener socket", EXIT_FAILURE);
	}
}

void bindCommunicatorToSourceAddress(const char* sourceAddress_string) noexcept {
	struct sockaddr sourceAddress = construct_sockaddr<CSA_RESOLVE_INTERFACES>(sourceAddress, 0);

	int enabler = true;
	if (setsockopt(communicatorSocket, IPPROTO_IP, IP_BIND_ADDRESS_NO_PORT, &enabler, sizeof(enabler)) == -1) {
		REPORT_ERROR_AND_EXIT("failed to enable IP_BIND_ADDRESS_NO_PORT with setsockopt", EXIT_FAILURE);
	}

	if (bind(communicatorSocket, &sourceAddress, sizeof(sourceAddress)) == -1) {
		switch (errno) {
		case EACCES:
			REPORT_ERROR_AND_EXIT("permission to bind socket to source address denied by local system", EXIT_SUCCESS);
		default:
			REPORT_ERROR_AND_EXIT("bind socket to source address failed, unknown reason", EXIT_FAILURE);
		}
	}
}

void NetworkShepherd::createCommunicatorAndConnect(const char* destinationAddress, uint16_t destinationPort, const char* sourceAddress) noexcept {
	struct sockaddr connectionTargetAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(destinationAddress, sourceAddress);

	communicatorSocket = socket(connectionTargetAddress.sa_family, SOCK_STREAM, 0);
	if (communicatorSocket == -1) { REPORT_ERROR_AND_EXIT("failed to construct connection communicator socket", EXIT_FAILURE); }

	if (connectionSourceAddress) { bindCommunicatorToSourceAddress(sourceAddress); }

	if (connect(communicatorSocket, &connectionTargetAddress, sizeof(connectionTargetAddress)) == -1) {
		switch (errno) {
			case EADDRNOTAVAIL:
				REPORT_ERROR_AND_EXIT("failed to connect, no ephemeral ports available", EXIT_FAILURE);
			case ECONNREFUSED:
				REPORT_ERROR_AND_EXIT("failed to connect, connection refused", EXIT_FAILURE);
			case ENETUNREACH:
				REPORT_ERROR_AND_EXIT("failed to connect, network unreachable", EXIT_FAILURE);
			case ETIMEDOUT:
				REPORT_ERROR_AND_EXIT("failed to connect, connection attempt timed out", EXIT_FAILURE);
			case EACCES: case EPERM:
				REPORT_ERROR_AND_EXIT("failed to connect, local system blocked attempt", EXIT_SUCCESS);
			default:
				REPORT_ERROR_AND_EXIT("failed to connect, unknown reason", EXIT_FAILURE);
		}
	}
}

size_t NetworkShepherd::read(void* buffer, size_t buffer_size) noexcept {
	void* buffer_start = buffer;
	while (true) {
		ssize_t bytesRead = read(communicatorSocket, buffer, buffer_size);
		if (bytesRead == 0) { return buffer - buffer_start; }
		if (bytesRead == -1) { REPORT_ERROR_AND_EXIT("failed to read from communicator socket", EXIT_FAILURE); }
		buffer += bytesRead;
		buffer_size -= bytesRead;
	}
}

void NetworkShepherd::write(void* buffer, size_t buffer_size) noexcept {
	while (true) {
		ssize_t bytesWritten = write(communicatorSocket, buffer, buffer_size);
		if (bytesWritten == buffer_size) { return; }
		if (bytesWritten == -1) { REPORT_ERROR_AND_EXIT("failed to write to communicator socket", EXIT_FAILURE); }
		buffer += bytesWritten;
		buffer_size -= bytesWritten;
	}
}

size_t NetworkShepherd::readUDP(void* buffer, size_t buffer_size) noexcept { return read(buffer, buffer_size); }

void NetworkShepherd::createUDPSender(const char* destinationAddress, uint16_t destinationPort, IPVersionConstraint destinationIPVersionConstraint, bool allowBroadcast, const char* sourceAddress) noexcept {
	UDPSenderTargetAddress = construct_sockaddr<CSA_RESOLVE_HOSTNAMES>(destinationAddress, destinationPort);

	switch (destinationIPVersionConstraint) {
	case IPVersionConstraint::NONE: break;
	case IPVersionConstraint::FOUR:
		if (UDPSenderTargetAddress.sa_family != AF_INET) {
			REPORT_ERROR_AND_EXIT("target address isn't IPv4", EXIT_SUCCESS);
		}
		break;
	case IPVersionConstraint::SIX:
		if (UDPSenderTargetAddress.sa_family != AF_INET6) {
			REPORT_ERROR_AND_EXIT("target address isn't IPv6", EXIT_SUCCESS);
		}
	}

	communicatorSocket = socket(UDPSenderTargetAddress.sa_family, SOCK_DGRAM, 0);
	if (communicatorSocket == -1) { REPORT_ERROR_AND_EXIT("failed to create UDP sender communicator socket", EXIT_FAILURE); }

	if (allowBroadcast) {
		int enabler = true;
		if (setsockopt(communicatorSocket, SOL_SOCKET, SO_BROADCAST, &enabler, sizeof(enabler)) == -1) {
			REPORT_ERROR_AND_EXIT("failed to allow broadcast on UDP sender socket with setsockopt", EXIT_FAILURE);
		}
	}

	if (sourceAddress) { bindCommunicatorToSourceAddress(sourceAddress); }
}

void NetworkShepherd::sendUDP(const void* buffer, size_t buffer_size) noexcept {
	while (true) {
		ssize_t bytesSent = sendto(connectionSocket, buffer, buffer_size, 0, &UDPSenderTargetAddress, sizeof(UDPSenderTargetAddress));
		if (bytesSent == buffer_size) { return; }
		if (bytesSent == -1) { REPORT_ERROR_AND_EXIT("failed to sendto on UDP sender communicator socket", EXIT_FAILURE); }
		buffer += bytesSent;
		buffer_size -= bytesSent;
	}
}

void NetworkShepherd::release() noexcept { }

void NetworkShepherd::closeCommunicator() noexcept {
	if (close(communicatorSocket) == -1) { REPORT_ERROR_AND_EXIT("failed to close communicator socket", EXIT_FAILURE); }
}

void NetworkShepherd::closeListener() noexcept {
	if (close(listenerSocket) == -1) {
		REPORT_ERROR_AND_EXIT("failed to close listener socket", EXIT_FAILURE);
	}
}
