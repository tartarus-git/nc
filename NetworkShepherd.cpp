#include "error_reporting.h"

template <bool resolve_interface_names>
sockaddr construct_sockaddr(const char* address, uint16_t port) noexcept {
	// TODO: Do the parsing and logic for this one.
}

void NetworkShepherd_Class::init() noexcept { }

void createListener(const char* address, uint16_t port, IPVersionConstraint listenerIPVersionConstraint) noexcept {
	sockaddr listenerAddress = construct_sockaddr<true>(address, port);

	listenerSocket = socket(listenerAddress.sa_family, SOCK_STREAM, 0);
	if (listenerSocket == -1) {
		REPORT_ERROR_AND_EXIT("failed to create listener socket", EXIT_FAILURE);
	}

	switch (listenerIPVersionConstraint) {
	case IPVersionConstraint::NONE:
		// TODO: do nothing on linux, on windows, set the socket to allow ipv4 connections even if it's ipv6.
	case IPVersionConstraint::FOUR:
		// TODO: setsockopt only allow ipv4 connections in (only do this when sa_family is ipv6 and your on linux I think)
	case IPVersionConstraint::SIX:
		// TODO: Do nothing unless sa_family is ipv4, if that's the case, throw an error about flag combinations and such.
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

void closeListener() noexcept {
	if (close(listenerSocket) == -1) {
		REPORT_ERROR_AND_EXIT("failed to close listener socket", EXIT_FAILURE);
	}
}

void release() noexcept { }

/*NetworkShepherd_Class::~NetworkShepherd_Class() noexcept {
	if (connectionSocket != -1) { 
}*/
