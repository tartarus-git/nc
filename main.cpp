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
	const char* sourceIP = nullptr;
	uint16_t sourcePort = 0;	// TODO: Actually use the source port.

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
	if (result > 9) { REPORT_ERROR_AND_EXIT("port input string is invalid", EXIT_SUCCESS); }
	for (size_t i = 1; portString[i] != '\0'; i++) {
		unsigned char digit = portString[i] - '0';
		if (digit > 9) { REPORT_ERROR_AND_EXIT("port input string is invalid", EXIT_SUCCESS); }
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
				continue;
			case '6':
				if (flags::IPVersionConstraint != IPVersionConstraint::NONE) {
					REPORT_ERROR_AND_EXIT("more than one IP version constraint specified", EXIT_SUCCESS);
				}
				flags::IPVersionConstraint = IPVersionConstraint::SIX;
				continue;
			case 'l':
				if (flags::shouldListen) {
					REPORT_ERROR_AND_EXIT("\"-l\" flag specified more than once", EXIT_SUCCESS);
				}
				flags::shouldListen = true;
				continue;
			case 'k':
				if (flags::shouldKeepListening) {
					REPORT_ERROR_AND_EXIT("\"-k\" flag specified more than once", EXIT_SUCCESS);
				}
				flags::shouldKeepListening = true;
				continue;
			case 'u':
				if (flags::shouldUseUDP) {
					REPORT_ERROR_AND_EXIT("\"-u\" flag specified more than once", EXIT_SUCCESS);
				}
				flags::shouldUseUDP = true;
				continue;
			case 'b':
				if (flags::allowBroadcast) {
					REPORT_ERROR_AND_EXIT("\"-b\" flag specified more than once", EXIT_SUCCESS);
				}
				flags::allowBroadcast = true;
				continue;
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
			continue;
		}
		switch (normalArgCount) {
		case 0: arguments::destinationIP = argv[i]; break;
		case 1: arguments::destinationPort = parsePort(argv[i]); break;
		default: REPORT_ERROR_AND_EXIT("too many non-flag args", EXIT_SUCCESS);
		}
		normalArgCount++;
	}

	if (normalArgCount < 2) { REPORT_ERROR_AND_EXIT("not enough non-flag args", EXIT_SUCCESS); }

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
	char buffer[65527];	// NOTE: We use the theoretical maximum data size for UDP packets here, since readUDP reads
				// packet-wise and discards whatever we don't catch in the buffer.
				// There isn't anything we can do about this AFAIK, we just try our best to get all the data.
				// IPv6 allows UDP packets to be bigger than this buffer by a lot, but there isn't anything we
				// can do about that either AFAIK (plus, no one in their right mind would make UDP packets bigger
				// than a couple hundred bytes anyway).
				// Reason for all this: it's too complicated to extract data and buffer it, so kernel
				// just only reads the current packet and discards the rest of packet if you didn't read enough.
				// The reason why it's too complicated is because UDP packets can come from many sources at once.
				// You would have to sort by source and have multiple different buffers, just so the user can read
				// in a nice way. If you instead decide to indiscriminately just buffer all the packets,
				// then the API becomes almost useless for the user in a lot of cases, because it's impossible
				// to determine which part of the byte stream came from which remote.
				// Basically, the fact that UDP is connectionless forces this on us.
	while (true) {
		size_t bytesRead = NetworkShepherd::readUDP(buffer, sizeof(buffer));
		if (bytesRead == 0) { continue; }
		char* buffer_ptr = buffer;
		while (true) {
			ssize_t bytesWritten = write(STDOUT_FILENO, buffer_ptr, bytesRead);
			if (bytesWritten == bytesRead) { break; }
			if (bytesWritten == -1) { REPORT_ERROR_AND_EXIT("failed to write to stdout", EXIT_FAILURE); }
			buffer_ptr += bytesWritten;
			bytesRead -= bytesWritten;
		}
	}
}

void do_UDP_send_and_close() noexcept {
	char buffer[BUFSIZ];		// TODO: Add an if somewhere so that it caps at the max packet length or something.
					// That way the sendto call wont fail because it doesn't fit in a packet.
	while (true) {
		ssize_t bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer));
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
		if (bytesRead == 0) { return; }
		char* buffer_ptr = buffer;
		while (true) {
			ssize_t bytesWritten = write(STDOUT_FILENO, buffer_ptr, bytesRead);
			if (bytesWritten == bytesRead) { break; }
			if (bytesWritten == -1) { REPORT_ERROR_AND_EXIT("failed to write to stdout", EXIT_FAILURE); }
			buffer_ptr += bytesWritten;
			bytesRead -= bytesWritten;
		}
	}
}

// TODO: after reading receives EOF when listening, you still have to write something in the TTY and hit enter for the program to close.
// If we had better communication between the threads, we could fix that. It's kind of challenging though because of the blocking calls.
// Maybe non-blocking is the way to go? Think about how to solve it.

void do_data_transfer_over_connection_and_close() noexcept {
	std::thread networkReadThread((void (*)())network_read_sub_transfer);

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
		NetworkShepherd::listen(0);		// TODO: Make the backlog changeable through a cmdline flag.

		if (flags::shouldKeepListening) {
			while (true) { accept_and_handle_connection(); }
		}

		accept_and_handle_connection();

		NetworkShepherd::closeListener();

		NetworkShepherd::release();

		return EXIT_SUCCESS;
	}

	if (flags::shouldUseUDP) {
		NetworkShepherd::createUDPSender(arguments::destinationIP, arguments::destinationPort, flags::allowBroadcast, flags::sourceIP, flags::IPVersionConstraint);
		do_UDP_send_and_close();

		NetworkShepherd::release();

		return EXIT_SUCCESS;
	}

	NetworkShepherd::createCommunicatorAndConnect(arguments::destinationIP, arguments::destinationPort, flags::sourceIP, flags::IPVersionConstraint);
	do_data_transfer_over_connection_and_close();

	NetworkShepherd::release();
}

// MAIN LOGIC END --------------------------------------------------------------
