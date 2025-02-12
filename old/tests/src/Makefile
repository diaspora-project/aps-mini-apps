CXX = clang++
# Where to find user code.
USER_DIR = ../src
TESTS_DIR = .

# Flags passed to the C++ compiler.
CXXFLAGS += -g -Wall -Wextra -pthread -std=c++11

INCLUDES = 	-I${USER_DIR}/common \
						-I${USER_DIR}/common/stream
LIBS = -lgtest -lgtest_main


# All tests produced by this Makefile.  Remember to add new tests you
# created to the list.
TESTS = trace_serialize_unittest


# House-keeping build targets.
all : $(TESTS)
clean :
	rm -f $(TESTS) *.o

# Builds a sample test.  A test should link with either gtest.a or
# gtest_main.a, depending on whether it defines its own main()
# function.

trace_serialize_unittest.o : $(TESTS_DIR)/trace_serialize_unittest.cc 
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $(TESTS_DIR)/trace_serialize_unittest.cc $(INCLUDES)

trace_serialize_unittest : trace_serialize_unittest.o
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -lpthread $(LIBS) $^ -o $@ 
