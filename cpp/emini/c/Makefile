# Makefile — E-mini S&P 500 limit order book, C implementation.
#
# Targets:
#   all       — build libbook.a (static library)
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
# The spec states: build with -O2 -Wconversion -Werror -std=c11.
# -march=native is added to honour the performance contract (TZCNT/LZCNT).

CC      := gcc
CFLAGS  := -std=c11 -O2 -march=native \
            -Wall -Wextra \
            -Wconversion -Wsign-conversion -Wsign-compare \
            -Wstrict-aliasing=2 \
            -Werror

AR      := ar
ARFLAGS := rcs

SRCS    := book.c matcher.c
OBJS    := $(SRCS:.c=.o)
LIB     := libbook.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

book.o: book.c book.h bitmap.h matcher.h
	$(CC) $(CFLAGS) -c -o $@ $<

matcher.o: matcher.c matcher.h book.h bitmap.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(LIB)
