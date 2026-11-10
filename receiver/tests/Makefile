# Host-side unit tests for the receiver's pure code.
#   make -C receiver/tests
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer
SRC := ../src/event_parser.cpp ../src/rx_state.cpp ../src/lcd_view.cpp ../src/rx_telemetry.cpp test_receiver.cpp test_interop.cpp \
       ../../src/communication/message_protocol.cpp ../../src/monitoring/severity_logic.cpp \
       ../../tests/unit/test_main.cpp
BIN := build/receiver_tests
.PHONY: all clean
all: $(BIN)
	./$(BIN)
$(BIN): $(SRC) $(wildcard ../src/*.h)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -I ../../tests/unit $(SRC) -o $@ -lm
clean:
	rm -rf build
