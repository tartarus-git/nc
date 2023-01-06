MAIN_CPP_INCLUDES := NetworkShepherd.h crossplatform_io.h error_reporting.h
NETWORK_SHEPHERD_INCLUDES := NetworkShepherd.h crossplatform_io.h error_reporting.h

BINARY_NAME := nc

CPP_STD := c++20
OPTIMIZATION_LEVEL := O3
USE_WALL := true
ifeq ($(USE_WALL), true)
POSSIBLE_WALL := -Wall
else
undefine POSSIBLE_WALL
endif

CLANG_PREAMBLE := clang++-11 -std=$(CPP_STD) -$(OPTIMIZATION_LEVEL) $(POSSIBLE_WALL) -fno-exceptions -pthread

.PHONY: all unoptimized clean

all: bin/$(BINARY_NAME)

unoptimized:
	$(MAKE) OPTIMIZATION_LEVEL:=O0

bin/$(BINARY_NAME): bin/main.o bin/NetworkShepherd.o
	$(CLANG_PREAMBLE) -o bin/$(BINARY_NAME) bin/main.o bin/NetworkShepherd.o

bin/main.o: main.cpp $(MAIN_CPP_INCLUDES) bin/.dirstamp
	$(CLANG_PREAMBLE) -c -I. -o bin/main.o main.cpp

bin/NetworkShepherd.o: NetworkShepherd.cpp $(NETWORK_SHEPHERD_INCLUDES) bin/.dirstamp
	$(CLANG_PREAMBLE) -c -I. -o bin/NetworkShepherd.o NetworkShepherd.cpp

bin/.dirstamp:
	mkdir -p bin
	touch bin/.dirstamp

clean:
	git clean -fdx
