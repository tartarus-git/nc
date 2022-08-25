#include "error_reporting.h"

template <bool resolve_interfaces_instead_of_hostnames>
sockaddr construct_sockaddr(const char* address, uint16_t port) noexcept {
	struct addrinfo addressRetrievalHint;
	addressRetrievalHint.ai_family = AF_UNSPEC;
	addressRetrievalHint.ai_socktype = 0;
	addressRetrievalHint.ai_protocol = 0;
	addressRetrievalHint.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG;

	if (resolve_interfaces_instead_of_hostnames) {
		struct ifaddrs* interfaceAddresses;

		if (getifaddrs(&interfaceAddresses) != -1) {

			for (struct ifaddrs* addr = interfaceAddresses; addr->ifa_next != nullptr; addr = info->ifa_next) {
				if (std::strcmp(addr->ifa_name, address) == 0) {
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

	switch (getaddrinfo(address, nullptr, nullptr, &addressInfo)) {
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

void NetworkShepherd_Class::init() noexcept { }

void createListener(const char* address, uint16_t port, int socketType, IPVersionConstraint listenerIPVersionConstraint) noexcept {
	struct sockaddr listenerAddress = construct_sockaddr<true>(address, port);

	listenerSocket = socket(listenerAddress.sa_family, socketType, 0);
	if (listenerSocket == -1) {
		REPORT_ERROR_AND_EXIT("failed to create listener socket", EXIT_FAILURE);
	}

	switch (listenerIPVersionConstraint) {
	case IPVersionConstraint::NONE:
		// TODO: do nothing on linux, on windows, set the socket to allow ipv4 connections even if it's ipv6.
	case IPVersionConstraint::FOUR:
		if (listenerAddress.sa_family = AF_INET6) {
			if (setsockopt(listenerSocket, SOL_SOCKET, ACCEPT_BOTH_THING, false, 0) == -1) {
				// TODO: How about fixing setsockopt call first before continuing.
			}
		}
	case IPVersionConstraint::SIX:
		if (listenerAddress.sa_family == AF_INET) {
			REPORT_ERROR_AND_EXIT("\"-6\" flag invalid with IPv4 address", EXIT_SUCCESS);
		}
		if (setsockopt(listenerSocket, SOL_SOCKET, ACCEPT_BOTH_THING, false, 0) == -1) {
			REPORT_ERROR_AND_EXIT("setsockopt failed for unknown reason", EXIT_FAILURE);
		}
	}

	if (bind(listenerSocket, &listenerAddress, sizeof(listenerAddress) == -1) {
		REPORT_ERROR_AND_EXIT("failed to bind listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd_Class::listen(int backlogLength) noexcept {
	if (listen(listenerSocket, backLogLength) == -1) {
		REPORT_ERROR_AND_EXIT("failed to listen with listener socket", EXIT_FAILURE);
	}
}

void NetworkShepherd_Class::accept() noexcept {
	connectionSocket = accept(listenerSocket, nullptr, nullptr);
	if (connectionSocket == -1) {
		REPORT_ERROR_AND_EXIT("failed to accept connection on listener socket", EXIT_FAILURE);
	}
}

void createCommunicatorAndConnect(const char* address, uint16_t port) noexcept {
	sockaddr connectionTargetAddress = construct_sockaddr<false>(address, port);

	connectionSocket = socket(connectionTargetAddress.sa_family, SOCK_STREAM, 0);

	if (connectionSocket == -1) {
		REPORT_ERROR_AND_EXIT("connection socket couldn't connect", EXIT_FAILURE);
	}
}

ssize_t read(void* buffer, size_t bufferSize) noexcept {
	ssize_t bytesRead = read(connectionSocket, buffer, bufferSize);
	if (bytesRead == -1) {
		REPORT_ERROR_AND_EXIT("failed to read from connection socket", EXIT_FAILURE);
	}
	return bytesRead;
}

ssize_t write(void* buffer, size_t bufferSize) noexcept {
	ssize_t bytesWritten = write(connectionSocket, buffer, bufferSize);
	if (bytesWritten == -1) {
		REPORT_ERROR_AND_EXIT("failed to write to connection socket", EXIT_FAILURE);
	}
	return bytesWritten;
}

void closeCommunicator() noexcept {
	if (close(connectionSocket) == -1) {
		REPORT_ERROR_AND_EXIT("failed to close connection socket", EXIT_FAILURE);
	}
}

void createUDPSender(const char* address, uint16_t port, IPVersionConstraint targetIPVersionConstraint) noexcept {
	UDPSenderTargetAddress = construct_sockaddr<false>(address, port);

	switch (targetIPVersionConstraint) {
	case IPVersionConstraint::NONE: break;
	case IPVersionConstraint::FOUR:
		if (UDPSenderTargetAddress.sa_family != AF_INET) {
			REPORT_ERROR_AND_EXIT("target address isn't IPv4", EXIT_SUCCESS);
		}
	case IPVersionConstraint::SIX:
		if (UDPSenderTargetAddress.sa_family != AF_INET6) {
			REPORT_ERROR_AND_EXIT("target address isn't IPv6", EXIT_SUCCESS);
		}
	}

	connectionSocket = socket(UDPSenderTargetAddress.sa_family, SOCK_DGRAM, 0);
	if (connectionSocket == -1) {
		REPORT_ERROR_AND_EXIT("failed to create UDP sender socket", EXIT_FAILURE);
	}
}

size_t sendUDP(const void* buffer, size_t size) noexcept {
	sendto(connectionSocket, buffer, size, 0, &UDPSenderTargetAddress, sizeof(UDPSenderTargetAddress));
	// TODO: Did I do the addrlen thing right?
}

void closeUDPSender() noexcept {
	closeCommunicator();
}

void closeListener() noexcept {
	if (close(listenerSocket) == -1) {
		REPORT_ERROR_AND_EXIT("failed to close listener socket", EXIT_FAILURE);
	}
}

void release() noexcept { }

/*NetworkShepherd_Class::~NetworkShepherd_Class() noexcept {
	if (connectionSocket != -1) { 
}*/
