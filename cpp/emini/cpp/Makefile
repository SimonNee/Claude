# Makefile — E-mini S&P 500 limit order book (C++ implementation)
#
# Architecture: architect-spec.md
#
# Three build targets:
#   lib  — static library (default): book.o + matcher.o → libemini.a
#   test — correctness build: sanitizers on, LTO off  (Idiom 9)
#   bench— benchmark build:  LTO on,   sanitizers off (Idiom 9)

CXX      := g++
AR       := ar

# Flags common to all builds
CXXFLAGS_COMMON := \
    -std=c++17 \
    -O2 \
    -march=native \
    -Wall \
    -Wextra \
    -Wconversion \
    -Wsign-conversion \
    -Werror \
    -fno-exceptions

# Correctness build — sanitizers on, LTO off (Idiom 9: tests build without -flto)
CXXFLAGS_TEST := $(CXXFLAGS_COMMON) \
    -fsanitize=undefined,address \
    -g

# Benchmark build — LTO on, sanitizers off (Idiom 9)
CXXFLAGS_BENCH := $(CXXFLAGS_COMMON) \
    -flto

# Default: production object files (no sanitizers, no LTO)
CXXFLAGS := $(CXXFLAGS_COMMON)

SRCS := book.cpp matcher.cpp
OBJS := book.o matcher.o

LIB  := libemini.a

.PHONY: all lib test-objs bench-objs bench clean

all: lib

lib: $(LIB)

$(LIB): $(OBJS)
	$(AR) rcs $@ $^

book.o: book.cpp book.hpp internal.hpp matcher.hpp
	$(CXX) $(CXXFLAGS) -c book.cpp -o $@

matcher.o: matcher.cpp matcher.hpp book.hpp internal.hpp
	$(CXX) $(CXXFLAGS) -c matcher.cpp -o $@

loader.o: loader.cpp loader.hpp book.hpp
	$(CXX) $(CXXFLAGS) -c loader.cpp -o $@

# Correctness objects (used by agentTest when it builds the test harness)
test-objs:
	$(CXX) $(CXXFLAGS_TEST) -c book.cpp    -o book_test.o
	$(CXX) $(CXXFLAGS_TEST) -c matcher.cpp -o matcher_test.o
	$(CXX) $(CXXFLAGS_TEST) -c loader.cpp  -o loader_test.o

# Benchmark objects (used by agentTest when it builds the benchmark harness)
bench-objs:
	$(CXX) $(CXXFLAGS_BENCH) -c book.cpp    -o book_bench.o
	$(CXX) $(CXXFLAGS_BENCH) -c matcher.cpp -o matcher_bench.o
	$(CXX) $(CXXFLAGS_BENCH) -c loader.cpp  -o loader_bench.o

# Data-driven benchmark binary
bench: bench_book_cpp

bench_book_cpp: bench-objs bench_book.cpp book.hpp loader.hpp internal.hpp matcher.hpp \
                book_bench.o matcher_bench.o loader_bench.o
	$(CXX) $(CXXFLAGS_BENCH) bench_book.cpp \
	    book_bench.o matcher_bench.o loader_bench.o \
	    -o bench_book_cpp

clean:
	rm -f $(OBJS) $(LIB) \
	      book_test.o matcher_test.o loader_test.o \
	      book_bench.o matcher_bench.o loader_bench.o \
	      bench_book_cpp
