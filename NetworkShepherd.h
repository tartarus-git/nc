#pragma once

class NetworkShepherd_Class {
	int listenerSocket;
	int connectionSocket;

	struct sockaddr UDPSenderTargetAddress;

	void init() noexcept;

	void createListener(const char* address, uint16_t port, int socketType, IPVersionConstraint listenerIPVersionConstraint) noexcept;

	void listen() noexcept;
	void accept() noexcept;

	void createCommunicatorAndConnect(const char* address, uint16_t port) noexcept;

	size_t read(void* buffer, size_t size) noexcept;
	size_t write(const void* buffer, size_t size) noexcept;

	void closeCommunicator() noexcept;

	void createUDPSender(const char* address, uint16_t port) noexcept;

	size_t sendUDP(const void* buffer, size_t size) noexcept;

	void closeUDPSender() noexcept;

	void closeListener() noexcept;

	void release() noexcept;

	// Destructor handles releasing things in emergencies.
	// If the program is not in the middle of an emergency
	// exit, release things yourself, as error messages
	// have the opportunity to get generated if you do that.
	// If program exits and things are not released,
	// this destructor will do it for you, but it won't
	// report errors in the case that releasing something
	// doesn't work as expected.
	//~NetworkShepherd_Class() noexcept;
} NetworkShepherd;
