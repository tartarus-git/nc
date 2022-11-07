#include <cstdlib>		// for std::exit(), EXIT_SUCCESS and EXIT_FAILURE, as well as most other syscalls
#include <cstdint>		// for fixed-width integer types
#include <cstring>		// for std::strcmp
#include <thread>		// for multi-threading

#include "NetworkShepherd.h"	// for NetworkShepherd class, which serves as our interface with the network

#include "crossplatform_io.h"

#include "error_reporting.h"	// definitely not for error reporting *wink*

/*
NOTE: Exit code is EXIT_SUCCESS on successful execution and on error resulting from invalid args.
Exit code is EXIT_FAILURE on every other error.
*/

// NOTE: I don't think there is a good way to #ifdef inside of multi-line strings in C/C++, which is why we opted to just change
// the help text here (I'm referring to the IMPORTANT: thing).

const char helpText[] = "usage: nc [-46lkub] [--source <source> || --port <source-port>] <address> <port>\n" \
			"       nc --help\n" \
			"\n" \
			"function: nc (netcat) sends and receives data over a network (no flags: initiate TCP connection to <address> on <port>)\n" \
			"\n" \
			"IMPORTANT: On Windows, interface recognition is disabled. Only hostnames and IPs are valid.\n" \
			"\n" \
			"arguments:\n" \
				"\t--help                       --> show help text\n" \
				"\t[-4 || -6]                   --> force data transfer over IPv6/IPv4\n" \
				"\t[-l]                         --> listen for connections on <address> and <port>\n" \
				"\t[-k]                         --> (only valid with -l) keep listening after connection terminates\n" \
				"\t[-u]                         --> use UDP (default: TCP)\n" \
				"\t[-b]                         --> (only valid with -u) allow broadcast addresses\n" \
				"\t[--source <source>]          --> (only valid without -l) send from <source> (can be IP/interface)\n" \
				"\t[--port <source-port>]       --> (only valid without -l and with --source*) send from <source-port>\n" \
				"\t[--backlog <backlog-length>] --> (only valid with -k) set backlog length to <backlog-length>\n" /* TODO: Add default mention */ \
				"\t<address>                    --> send to <address> or (with -l) listen on <address> (can be IP/hostname/interface)\n" \
				"\t<port>                       --> send to <port> or (with -l) listen on <port>\n" \
			"\n" \
			"notes:\n" \
				"\t* The exception to the rule is \"--port 0\". This is treated as a no-op and can also appear any amount of times\n" \
				"\tas long as \"--port\" hasn't been specified to the left of it with a non-zero value.\n";

// COMMAND-LINE PARSER START ---------------------------------------------------

namespace arguments {
	const char* destinationIP;
	uint16_t destinationPort;
}

namespace flags {
	const char* sourceIP = nullptr;
	uint16_t sourcePort = 0;

	IPVersionConstraint IPVersionConstraint = IPVersionConstraint::NONE;

	bool shouldListen = false;
	bool shouldKeepListening = false;
	int backlog = -1;

	bool shouldUseUDP = false;

	bool allowBroadcast = false;
}

uint16_t parsePort(const char* portString) noexcept {
	if (portString[0] == '\0') { REPORT_ERROR_AND_EXIT("port input string cannot be empty", EXIT_SUCCESS); }
	uint32_t result = portString[0] - '0';
	if (result > 9) { REPORT_ERROR_AND_EXIT("port input string is invalid", EXIT_SUCCESS); }
	for (size_t i = 1; portString[i] != '\0'; i++) {
		unsigned char digit = portString[i] - '0';
		if (digit > 9) { REPORT_ERROR_AND_EXIT("port input string is invalid", EXIT_SUCCESS); }
		result = result * 10 + digit;
		if (result > 0b1111111111111111) { REPORT_ERROR_AND_EXIT("port input value too large", EXIT_SUCCESS); }
	}
	return result;
}

int parseBacklog(const char* backlogString) noexcept {
	if (backlogString[0] == '\0') { REPORT_ERROR_AND_EXIT("backlog input string cannot be empty", EXIT_SUCCESS); }
	uint64_t result = backlogString[0] - '0';
	if (result > 9) { REPORT_ERROR_AND_EXIT("backlog input string is invalid", EXIT_SUCCESS); }
	for (size_t i = 1; backlogString[i] != '\0'; i++) {
		unsigned char digit = backlogString[i] - '0';
		if (digit > 9) { REPORT_ERROR_AND_EXIT("backlog input string is invalid", EXIT_SUCCESS); }
		result = result * 10 + digit;
		if (result > 0b01111111111111111111111111111111) { REPORT_ERROR_AND_EXIT("backlog input value too large", EXIT_SUCCESS); }
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

void validateFlagRelationships() noexcept {
	if (flags::shouldListen) {
		if (flags::allowBroadcast) { REPORT_ERROR_AND_EXIT("broadcast isn't allowed when listening", EXIT_SUCCESS); }

		if (flags::shouldKeepListening) {
			// TODO: Fix backlog system to default to 1 in the syscall?
			if (flags::shouldUseUDP) { REPORT_ERROR_AND_EXIT("\"-k\" cannot be specified with \"-u\"", EXIT_SUCCESS); }
		} else {
			if (flags::backlog != -1) { REPORT_ERROR_AND_EXIT("\"--backlog\" cannot be specified without \"-k\"", EXIT_SUCCESS); }
		}

		if (flags::sourceIP) { REPORT_ERROR_AND_EXIT("\"--source\" may not be used when listening", EXIT_SUCCESS); }

		if (flags::sourcePort != 0) { REPORT_ERROR_AND_EXIT("\"--port\" may not be used when listening unless the specified source port is 0", EXIT_SUCCESS); }
	} else {
		if (flags::shouldKeepListening) { REPORT_ERROR_AND_EXIT("\"-k\" cannot be specified without \"-l\"", EXIT_SUCCESS); }
	}

	if (!flags::shouldUseUDP) {
		if (flags::allowBroadcast) { REPORT_ERROR_AND_EXIT("broadcast is only allowed when sending UDP packets", EXIT_SUCCESS); }
	}

	if (!flags::sourceIP) {
		if (flags::sourcePort != 0) { REPORT_ERROR_AND_EXIT("\"--port\" cannot be specified without \"--source\" unless the specified source port is 0", EXIT_SUCCESS); }
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
						if (flags::sourceIP != nullptr) { REPORT_ERROR_AND_EXIT("\"--source\" cannot be specified more than once", EXIT_SUCCESS); }
						i++;
						if (i == argc) { REPORT_ERROR_AND_EXIT("\"--source\" requires an input value", EXIT_SUCCESS); }
						flags::sourceIP = argv[i];
						continue;
					}
					if (std::strcmp(flagContent, "port") == 0) {
						if (flags::sourcePort != 0) { REPORT_ERROR_AND_EXIT("\"--port\" cannot be specified more than once*", EXIT_SUCCESS); }
						i++;
						if (i == argc) { REPORT_ERROR_AND_EXIT("\"--port\" requires an input value", EXIT_SUCCESS); }
						flags::sourcePort = parsePort(argv[i]);
						continue;
					}
					if (std::strcmp(flagContent, "backlog") == 0) {
						if (flags::backlog != -1) { REPORT_ERROR_AND_EXIT("\"--backlog\" cannot be specified more than once", EXIT_SUCCESS); }
						i++;
						if (i == argc) { REPORT_ERROR_AND_EXIT("\"--backlog\" requires an input value", EXIT_SUCCESS); }
						flags::backlog = parseBacklog(argv[i]);
						continue;
					}
					if (std::strcmp(flagContent, "help") == 0) {
						if (argc != 2) { REPORT_ERROR_AND_EXIT("use of \"--help\" flag with other args is illegal", EXIT_SUCCESS); }
						if (crossplatform_write(STDOUT_FILENO, helpText, sizeof(helpText) - 1) == -1) {
							REPORT_ERROR_AND_EXIT("failed to write to stdout", EXIT_FAILURE);
						}
						halt_program(EXIT_SUCCESS);
					}
					REPORT_ERROR_AND_EXIT("one or more invalid flags specified", EXIT_SUCCESS);
				}
			default: parseLetterFlags(flagContent); continue;
			}
		}

		switch (normalArgCount) {
		case 0: arguments::destinationIP = argv[i]; break;
		case 1: arguments::destinationPort = parsePort(argv[i]); break;
		default: REPORT_ERROR_AND_EXIT("too many non-flag args", EXIT_SUCCESS);
		}
		normalArgCount++;
	}

	if (normalArgCount < 2) { REPORT_ERROR_AND_EXIT("not enough non-flag args", EXIT_SUCCESS); }

	validateFlagRelationships();
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
				// IPv6 allows UDP packets to be bigger than this buffer by a lot (through weird extentions), but there isn't anything we
				// can do about that either AFAIK (plus, no one in their right mind would make UDP packets bigger
				// than a couple hundred bytes anyway).
				// I don't even think those jumbogram things can get to this endpoint because we didn't tell the endpoint to
				// accept those and I think they might require special treatment.
				// Reason for all this: it's too complicated to extract data and buffer it, so kernel
				// just only reads the current packet and discards the rest of packet if you didn't read enough.
				// The reason why it's too complicated is because UDP packets can come from many sources at once.
				// You would have to sort by source and have multiple different buffers, just so the user can read
				// in a nice way. If you instead decide to indiscriminately just buffer all the packets,
				// then the API becomes almost useless for the user in a lot of cases, because it's impossible
				// to determine which part of the byte stream came from which remote.
				// Basically, the fact that UDP is connectionless forces this on us.

				// NOTE: Actually, you can "connect" a UDP socket so that it only receives data from one endpoint.
				// In that case, a more streamed reading system would be possible, but I assume Linux doesn't work this way
				// because it would be confusing and annoying to have the behaviour change around just like that.
				// I'm not sure though, maybe you can research it a bit more. TODO.
	while (true) {
		size_t bytesRead = NetworkShepherd::readUDP(buffer, sizeof(buffer));
		if (bytesRead == 0) { continue; }
		char* buffer_ptr = buffer;
		while (true) {
			sioret_t bytesWritten = crossplatform_write(STDOUT_FILENO, buffer_ptr, bytesRead);
			if (bytesWritten == bytesRead) { break; }
			if (bytesWritten == -1) { REPORT_ERROR_AND_EXIT("failed to write to stdout", EXIT_FAILURE); }
			buffer_ptr += bytesWritten;
			bytesRead -= bytesWritten;
		}
	}
}

void do_UDP_send_and_close() noexcept {
	NetworkShepherd::enableFindMSS();

	uint16_t buffer_size = NetworkShepherd::getMSSApproximation();
	char* buffer = new (std::nothrow) char[buffer_size];
	if (!buffer) { REPORT_ERROR_AND_EXIT("failed to allocate buffer", EXIT_FAILURE); }

	while (true) {
		sioret_t bytesRead = crossplatform_read(STDIN_FILENO, buffer, buffer_size);
		if (bytesRead == 0) { break; }
		if (bytesRead == -1) { REPORT_ERROR_AND_EXIT("failed to read from stdin", EXIT_FAILURE); }

		uint16_t newMSS = NetworkShepherd::writeUDPAndFindMSS(buffer, bytesRead);
		if (newMSS == 0) { continue; }		// NOTE: newMSS == 0 means MSS stays the same.

		delete[] buffer;
		buffer = new (std::nothrow) char[newMSS];
		if (!buffer) { REPORT_ERROR_AND_EXIT("failed to reallocate buffer", EXIT_FAILURE); }
		buffer_size = newMSS;
	}

	NetworkShepherd::closeCommunicator();

	delete[] buffer;
}

#define NRST_CLOSE_STDOUT_ON_FINISH true
#define NRST_LEAVE_STDOUT_OPEN false

template <bool close_stdout_on_finish>
void network_read_sub_transfer() noexcept {
	char buffer[BUFSIZ];
	while (true) {
		size_t bytesRead = NetworkShepherd::read(buffer, sizeof(buffer));
		if (bytesRead == 0) {
			if (close_stdout_on_finish) {
				if (close(STDOUT_FILENO) == -1) { REPORT_ERROR_AND_EXIT("failed to close stdout fd", EXIT_FAILURE); }
			}
			return;
		}
		char* buffer_ptr = buffer;
		while (true) {
			sioret_t bytesWritten = crossplatform_write(STDOUT_FILENO, buffer_ptr, bytesRead);
			if (bytesWritten == bytesRead) { break; }
			if (bytesWritten == -1) { REPORT_ERROR_AND_EXIT("failed to write to stdout", EXIT_FAILURE); }
			buffer_ptr += bytesWritten;
			bytesRead -= bytesWritten;
		}
	}
}

template <bool close_stdout_on_finish>
void do_data_transfer_over_connection_and_close() noexcept {
	std::thread networkReadThread((void (*)())network_read_sub_transfer<close_stdout_on_finish>);

	char buffer[BUFSIZ];
	while (true) {
		sioret_t bytesRead = crossplatform_read(STDIN_FILENO, buffer, sizeof(buffer));
		if (bytesRead == 0) { NetworkShepherd::shutdownCommunicatorWrite(); break; }
		if (bytesRead == -1) { REPORT_ERROR_AND_EXIT("failed to read from stdin", EXIT_FAILURE); }
		NetworkShepherd::write(buffer, bytesRead);
	}

	networkReadThread.join();

	NetworkShepherd::closeCommunicator();
}

template <bool close_stdout_on_finish>
void accept_and_handle_connection() noexcept {
	NetworkShepherd::accept();
	do_data_transfer_over_connection_and_close<close_stdout_on_finish>();
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
		// TODO: I still don't understand the backlog parameter. It doesn't seem to do anything on any of the OS's I test it on.
		// Can you please find some documentation that explains what the hell is going on? Or ask on stackoverflow.
		NetworkShepherd::listen(flags::backlog == -1 ? 1 : flags::backlog);

		if (flags::shouldKeepListening) {
			while (true) { accept_and_handle_connection<NRST_LEAVE_STDOUT_OPEN>(); }
		}

		accept_and_handle_connection<NRST_CLOSE_STDOUT_ON_FINISH>();

		NetworkShepherd::closeListener();

		NetworkShepherd::release();

		return EXIT_SUCCESS;
	}

	if (flags::shouldUseUDP) {
		NetworkShepherd::createUDPSender(arguments::destinationIP, arguments::destinationPort, flags::allowBroadcast, flags::sourceIP, flags::sourcePort, flags::IPVersionConstraint);
		do_UDP_send_and_close();

		NetworkShepherd::release();

		return EXIT_SUCCESS;
	}

	NetworkShepherd::createCommunicatorAndConnect(arguments::destinationIP, arguments::destinationPort, flags::sourceIP, flags::sourcePort, flags::IPVersionConstraint);
	do_data_transfer_over_connection_and_close<NRST_CLOSE_STDOUT_ON_FINISH>();

	NetworkShepherd::release();
}

// MAIN LOGIC END --------------------------------------------------------------
