#include <cstdlib>	// for EXIT_SUCCESS and EXIT_FAILURE, as well as every syscall we use
#include <cstdint>	// for fixed-width integer types

#include "error_reporting.h"

const char helpText[] = "usage: nc [-46lkbu] [--source <source> || --port <source-port>] [<address> <port>]\n" \
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
				"\t[--source <source>]    --> (only valid without -l) send from <source> (can be IP/interface)\n" \
				"\t[--port <source-port>] --> (only valid without -l) send from <source-port>\n" \
				"\t[<address>]            --> send to <address> or (with -l) listen on <address> (can be IP/hostname/interface)\n" \
				"\t[<port>]               --> send to <port> or (with -l) listen on <port>\n";

// COMMAND-LINE PARSER START ---------------------------------------------------

enum class IPVersionConstraint : uint8_t {
	NONE,
	FOUR,
	SIX
};

namespace flags {
	const char* sourceIP;
	uint16_t sourcePort;

	IPVersionConstraint IPVersionConstraint = IPVersionConstraint::NONE;

	bool shouldListen = false;
	bool shouldKeepListening = false;

	bool shouldUseUDP = false;
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
			// TODO: Can you listen on an ipv6 addr and receive ipv4 traffic? Yes you can actually.
			// TODO: That means you need to throw error when -4 is specified and the address is an ipv6 address for example.
			// No you don't, you only need to throw an error when an ipv4 address is specified and -6 is used, because that really isn't possible.
			// Accepting ipv4 connections on ipv6 listeners works, there is more to it though and the cross-platform stuff is something you have to be wary
			// of.
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
					REPORT_ERROR_AND_EXIT("\"-u\" flag used more than once", EXIT_SUCCESS);
				}
				flags::shouldUseUDP = true;
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
	}
	// TODO: Order the above checks to make the most sense efficiency and probability-wise and rethink which tests you want to nest and double-check and such.
}

// COMMAND-LINE PARSER END -----------------------------------------------------

// MAIN LOGIC START ------------------------------------------------------------

void do_TCP_data_transfer_over_connection() noexcept {
	while (true) {
		NetworkShepherd.read();// TODO: etc...
		NetworkShepherd.write();
	}

	NetworkShepherd.closeConnection();
}

void do_UDP_data_transfer_over_connection() noexcept {
	// TODO: etc...
}

void accept_and_handle_connection() noexcept {
	NetworkShepherd.accept();
}

int main(int argc, const char* const * argv) noexcept {
	manageArgs(argc, argv);

	NetworkShepherd.init();

	// TODO: Put in the source port stuff, I assume you use setsockopt for that.

	if (flags::shouldListen) {
		NetworkShepherd.listen(arguments::destinationIP, arguments::destinationPort);

		if (flags::shouldKeepListening) {
			while (true) { accept_and_handle_connection(); }
			return 0;
		}
		accept_and_handle_connection();

		return 0;
	}

	if (flags::shouldUseUDP) {
		NetworkShepherd.createUDPSender(arguments::destinationIP, arguments::destinationPort);

		do_UDP_data_transfer_over_connection();

		return 0;
	}

	NetworkShepherd.createCommunicatorAndConnect(arguments::destinationIP, arguments::destinationPort);

	do_data_transfer_over_connection();
}

// MAIN LOGIC END --------------------------------------------------------------
