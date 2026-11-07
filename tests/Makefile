# Host-side unit tests for the platform-independent firmware core.
#   make -C tests        build and run
#   make -C tests clean
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -Wno-unused-function \
            -fsanitize=address,undefined -fno-omit-frame-pointer

SRC_CORE := ../src/monitoring/severity_logic.cpp \
            ../src/monitoring/air_quality_monitor.cpp \
            ../src/monitoring/environment_monitor.cpp \
            ../src/monitoring/alert_manager.cpp \
            ../src/communication/message_protocol.cpp \
            ../src/communication/ack_manager.cpp \
            ../src/communication/rylr998_codec.cpp \
            ../src/actuators/buzzer_pattern.cpp \
            ../src/display/screen_logic.cpp \
            ../src/utils/text_utils.cpp

SRC_TEST := $(wildcard unit/*.cpp)
BIN      := build/unit_tests

.PHONY: all test clean
all: test

$(BIN): $(SRC_CORE) $(SRC_TEST) $(wildcard unit/*.h) $(wildcard ../src/*/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SRC_CORE) $(SRC_TEST) -o $@ -lm

test: $(BIN)
	./$(BIN)

clean:
	rm -rf build
