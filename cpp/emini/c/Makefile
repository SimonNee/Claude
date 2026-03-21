# Makefile — E-mini S&P 500 limit order book, C implementation.
#
# Targets:
#   all       — build libbook.a (static library)
#   bench     — build bench_book binary (data-driven, requires orders.csv)
#   test      — build test_book binary
#   clean     — remove build artefacts
#
# Flags match the spec:
#   -std=c11  (spec says c11; pitfalls.md references c17 for dev but c11
#              is the stated requirement)
#   -O2       — optimisation required for __builtin_ctzll → TZCNT
#   -march=native — enables TZCNT/LZCNT as single instructions
#   -Wconversion -Wsign-conversion -Wsign-compare — enforce no-cast rule
#   -Werror   — every warning is a build failure
#
# Benchmark build adds -flto for link-time optimisation (no sanitizers).

CC      := gcc
CFLAGS  := -std=c11 -O2 -march=native \
            -Wall -Wextra \
            -Wconversion -Wsign-conversion -Wsign-compare \
            -Wstrict-aliasing=2 \
            -Werror

CFLAGS_BENCH := $(CFLAGS) -flto -D_POSIX_C_SOURCE=200809L

AR      := ar
ARFLAGS := rcs

LIB_SRCS  := book.c matcher.c
LIB_OBJS  := $(LIB_SRCS:.c=.o)
LIB       := libbook.a

LOADER_SRCS := loader.c
LOADER_OBJS := $(LOADER_SRCS:.c=.o)

.PHONY: all bench test clean

all: $(LIB)

# ------------------------------------------------------------------
# Library
# ------------------------------------------------------------------

$(LIB): $(LIB_OBJS)
	$(AR) $(ARFLAGS) $@ $^

book.o: book.c book.h bitmap.h matcher.h
	$(CC) $(CFLAGS) -c -o $@ $<

matcher.o: matcher.c matcher.h book.h bitmap.h
	$(CC) $(CFLAGS) -c -o $@ $<

# ------------------------------------------------------------------
# Loader (compiled with same CFLAGS as the library)
# ------------------------------------------------------------------

loader.o: loader.c loader.h book.h
	$(CC) $(CFLAGS) -c -o $@ $<

# ------------------------------------------------------------------
# Benchmark binary
# ------------------------------------------------------------------

bench: bench_book

bench_book: bench_book.c book.h loader.h matcher.h \
            book.o matcher.o loader.o
	$(CC) $(CFLAGS_BENCH) -o $@ bench_book.c book.o matcher.o loader.o -lm

# ------------------------------------------------------------------
# Test binary
# ------------------------------------------------------------------

test: test_book

test_book: test_book.c book.h matcher.h \
           book.o matcher.o
	$(CC) $(CFLAGS) -o $@ test_book.c book.o matcher.o -lm

# ------------------------------------------------------------------
# Clean
# ------------------------------------------------------------------

clean:
	rm -f $(LIB_OBJS) $(LOADER_OBJS) $(LIB) bench_book test_book
