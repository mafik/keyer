# Makefile for keyer_simulator tests

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O3
LDFLAGS = -lgtest -lgtest_main -lpthread

TEST_TARGET = keyer_simulator_test
TEST_SRC = keyer_simulator_test.cpp

.PHONY: all test clean

all: test

$(TEST_TARGET): $(TEST_SRC) fingers.cpp
	$(CXX) $(CXXFLAGS) $(TEST_SRC) -o $(TEST_TARGET) $(LDFLAGS)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(TEST_TARGET)
