#include <cstdlib>

[[noreturn]] inline void halt_program(int exit_code) noexcept {
	std::exit(exit_code);
	while (true) { }
}
