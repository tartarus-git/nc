#include <cstdlib>		// for EXIT_SUCCESS and EXIT_FAILURE, as well as every syscall we use
#include <cstdint>		// for fixed-width integer types
#include <cstring>		// for std::strcmp
#include <unistd.h>		// for linux I/O
#include <thread>		// for multi-threading

#include "NetworkShepherd.h"	// for NetworkShepherd class, which serves as our interface with the network

#include "error_reporting.h"

const char helpText[] = "usage: nc [-46lkub] [--source <source> || --port <source-port>] [<address> <port>]\n" \
			"       nc --help\n" \
			"\n" \
			"function: nc (netcat) sends and receives data over a network (no flags: initiate TCP connection to <address> on <port>)\n" \
			"\n" \
			"arguments:\n" \
				"\t--help                 --> show help text\n" \
				"\t[-4 || -6]             --> force data transfer over IPv6/IPv4 (default: prefer IPv6)\n" \
				"\t[-l]                   --> listen for connections on <address> and <port>\n" \
				"\t[-k]                   --> (only valid with -l) keep listening after connection terminates\n" \
				"\t[-u]                   --> use UDP (default: TCP)\n" \
				"\t[-b]                   --> allow broadcast addresses\n" \
				"\t[--source <source>]    --> (only valid without -l) send from <source> (can be IP/interface)\n" \
				"\t[--port <source-port>] --> (only valid without -l) send from <source-port>\n" \
				"\t[<address>]            --> send to <address> or (with -l) listen on <address> (can be IP/hostname/interface)\n" \
				"\t[<port>]               --> send to <port> or (with -l) listen on <port>\n";

// COMMAND-LINE PARSER START ---------------------------------------------------

namespace flags {
	const char* sourceIP;
	uint16_t sourcePort;

	IPVersionConstraint IPVersionConstraint = IPVersionConstraint::NONE;

	bool shouldListen = false;
	bool shouldKeepListening = false;

	bool shouldUseUDP = false;

	bool allowBroadcast = false;
}

namespace arguments {
	const char* destinationIP;
	uint16_t destinationPort;
}

uint16_t parsePort(const char* portString) noexcept {
	if (portString[0] == '\0') { REPORT_ERROR_AND_EXIT("port input string cannot be empty", EXIT_SUCCESS); }
	uint16_t result = portString[0] - '0';
	if (result < 0 || result > 9) { REPORT_ERROR_AND_EXIT("port input string is invalid", EXIT_SUCCESS); }
	for (size_t i = 0; portString[i] != '\0'; i++) {
		unsigned char digit = portString[i] - '0';
		if (digit < 0 || digit > 9) { REPORT_ERROR_AND_EXIT("port input string is invalid", EXIT_SUCCESS); }
		result = result * 10 + digit;
	}
	return result;
}

void parseLetterFlags(const char* flagContent) noexcept {
	for (size_t i = 0; flagContent[i] != '\0'; i++) {
		switch (flagContent[i]) {
			case '4':
				if (flags::IPVersionConstraint != IPVersionConstraint::NONE) {
					REPORT_ERROR_AND_EXIT("more than one IP version constraint specified", EXIT_SUCCESS);
				}
				flags::IPVersionConstraint = IPVersionConstraint::FOUR;
			case '6':
				if (flags::IPVersionConstraint != IPVersionConstraint::NONE) {
					REPORT_ERROR_AND_EXIT("more than one IP version constraint specified", EXIT_SUCCESS);
				}
				flags::IPVersionConstraint = IPVersionConstraint::SIX;
			case 'l':
				if (flags::shouldListen) {
					REPORT_ERROR_AND_EXIT("\"-l\" flag specified more than once", EXIT_SUCCESS);
				}
				flags::shouldListen = true;
			case 'k':
				if (flags::shouldKeepListening) {
					REPORT_ERROR_AND_EXIT("\"-k\" flag specified more than once", EXIT_SUCCESS);
				}
				flags::shouldKeepListening = true;
			case 'u':
				if (flags::shouldUseUDP) {
					REPORT_ERROR_AND_EXIT("\"-u\" flag specified more than once", EXIT_SUCCESS);
				}
				flags::shouldUseUDP = true;
			case 'b':
				if (flags::allowBroadcast) {
					REPORT_ERROR_AND_EXIT("\"-b\" flag specified more than once", EXIT_SUCCESS);
				}
				flags::allowBroadcast = true;
			default: REPORT_ERROR_AND_EXIT("one or more invalid flags specified", EXIT_SUCCESS);
		}
	}
}

void manageArgs(int argc, const char* const * argv) noexcept {
	unsigned char normalArgCount = 0;
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-') {
			const char* flagContent = argv[i] + 1;
			switch (*flagContent) {
			case '-':
				{
					flagContent++;
					if (std::strcmp(flagContent, "source") == 0) {
						i++;
						flags::sourceIP = argv[i];
						continue;
					}
					if (std::strcmp(flagContent, "port") == 0) {
						i++;
						flags::sourcePort = parsePort(argv[i]);
						continue;
					}
					if (std::strcmp(flagContent, "help") == 0) {
						if (argc != 2) { REPORT_ERROR_AND_EXIT("use of \"--help\" flag with other args is illegal", EXIT_SUCCESS); }
						if (write(STDOUT_FILENO, helpText, sizeof(helpText) - 1) == -1) {
							REPORT_ERROR_AND_EXIT("failed to write to stdout", EXIT_FAILURE);
						}
						std::exit(EXIT_SUCCESS);
					}
					REPORT_ERROR_AND_EXIT("one or more invalid flags specified", EXIT_SUCCESS);
				}
			default: parseLetterFlags(flagContent);
			}
		}
		switch (normalArgCount) {
		case 0: arguments::destinationIP = argv[i]; break;
		case 1: arguments::destinationPort = parsePort(argv[i]); break;
		default: REPORT_ERROR_AND_EXIT("too many non-flag args", EXIT_SUCCESS);
		}
		normalArgCount++;
	}

	if (!flags::shouldListen) {
		if (flags::shouldKeepListening) { REPORT_ERROR_AND_EXIT("\"-k\" cannot be specified without \"-l\"", EXIT_SUCCESS); }
	} else {
		if (flags::allowBroadcast) { REPORT_ERROR_AND_EXIT("broadcast isn't allowed when listening", EXIT_SUCCESS); }
	}
	if (!flags::shouldUseUDP) {
		if (flags::allowBroadcast) { REPORT_ERROR_AND_EXIT("broadcast is only allowed when sending UDP packets", EXIT_SUCCESS); }
	}
	if (flags::shouldKeepListening) {
		if (flags::shouldUseUDP) { REPORT_ERROR_AND_EXIT("\"-k\" cannot be specified with \"-u\"", EXIT_SUCCESS); }
	}
	// TODO: Order the above checks to make the most sense efficiency and probability-wise and rethink which tests you want to nest and double-check and such.
}

// COMMAND-LINE PARSER END -----------------------------------------------------

// MAIN LOGIC START ------------------------------------------------------------

// NOTE: One never returns from this function, since UDP sockets can only get closed properly by the local user.
// NOTE: When the local user sends SIGINT, the program abruptly terminates and we rely on the OS to clean up the UDP socket.
// NOTE: That's why we don't do it here.
void do_UDP_receive() noexcept {
	char buffer[BUFSIZ];
	while (true) {
		size_t bytesRead = NetworkShepherd::readUDP(buffer, sizeof(buffer));
		while (true) {
			ssize_t bytesWritten = write(buffer, bytesRead);
			if (bytesWritten == bytesRead) { break; }
			if (bytesWritten == -1) { REPORT_ERROR_AND_EXIT("failed to write to stdout", EXIT_FAILURE); }
			bytesRead -= bytesWritten;
		}
	}
}

void do_UDP_send_and_close() noexcept {
	char buffer[BUFSIZ];
	while (true) {
		ssize_t bytesRead = read(buffer, sizeof(buffer));
		if (bytesRead == 0) { break; }
		if (bytesRead == -1) { REPORT_ERROR_AND_EXIT("failed to read from stdin", EXIT_FAILURE); }
		NetworkShepherd::writeUDP(buffer, bytesRead);
	}

	NetworkShepherd::closeCommunicator();
}

void network_read_sub_transfer() noexcept {
	char buffer[BUFSIZ];
	while (true) {
		ssize_t bytesRead = NetworkShepherd::read(buffer, sizeof(buffer));
		if (bytesRead == 0) { break; }
		while (true) {
			ssize_t bytesWritten = write(buffer, bytesRead);
			if (bytesWritten == bytesRead) { break; }
			if (bytesWritten == -1) { REPORT_ERROR_AND_EXIT("failed to write to stdout", EXIT_FAILURE); }
			bytesRead -= bytesWritten;
		}
	}
}

void do_data_transfer_over_connection_and_close() noexcept {
	std::thread networkReadThread(network_read_sub_transfer);

	char buffer[BUFSIZ];
	while (true) {
		ssize_t bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer));
		if (bytesRead == 0) { break; }
		if (bytesRead == -1) { REPORT_ERROR_AND_EXIT("failed to read from stdin", EXIT_FAILURE); }
		NetworkShepherd::write(buffer, bytesRead);
	}

	networkReadThread.join();

	NetworkShepherd::closeCommunicator();
}

void accept_and_handle_connection() noexcept {
	NetworkShepherd::accept();
	do_data_transfer_over_connection_and_close();
}

int main(int argc, const char* const * argv) noexcept {
	manageArgs(argc, argv);

	NetworkShepherd::init();

	if (flags::shouldListen) {
		if (flags::shouldUseUDP) {
			NetworkShepherd::createListener(arguments::destinationIP, arguments::destinationPort, SOCK_DGRAM, flags::IPVersionConstraint);
			do_UDP_receive();
			// NOTE: The above function never returns.
		}

		NetworkShepherd::createListener(arguments::destinationIP, arguments::destinationPort, SOCK_STREAM, flags::IPVersionConstraint);
		NetworkShepherd::listen(arguments::destinationIP, arguments::destinationPort);

		if (flags::shouldKeepListening) {
			while (true) { accept_and_handle_connection(); }
		}

		accept_and_handle_connection();

		NetworkShepherd::closeListener();

		NetworkShepherd::release();

		return EXIT_SUCCESS;
	}

	if (flags::shouldUseUDP) {
		NetworkShepherd::createUDPSender(arguments::destinationIP, arguments::destinationPort);
		do_UDP_send();

		NetworkShepherd::release();

		return EXIT_SUCCESS;
	}

	NetworkShepherd.createCommunicatorAndConnect(arguments::destinationIP, arguments::destinationPort);
	do_data_transfer_over_connection();

	NetworkShepherd::release();
}

// MAIN LOGIC END --------------------------------------------------------------
