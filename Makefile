# Builds the four search programs with clang. On Windows the 128-bit and 256-bit
# programs need compiler-rt for 128-bit division helpers; on Linux and macOS the
# flag is harmless.
CC ?= clang
CFLAGS ?= -O3 -march=native -Wall -Wextra
RTLIB ?= -rtlib=compiler-rt
BIN ?= bin

PROGRAMS = $(BIN)/tripal $(BIN)/tripal_hex $(BIN)/tripal128 $(BIN)/tripal_wide

all: $(PROGRAMS)

$(BIN):
	mkdir -p $(BIN)

$(BIN)/tripal: src/tripal.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/tripal_hex: src/tripal_hex.c | $(BIN)
	$(CC) $(CFLAGS) -o $@ $<

$(BIN)/tripal128: src/tripal128.c | $(BIN)
	$(CC) $(CFLAGS) $(RTLIB) -o $@ $<

$(BIN)/tripal_wide: src/tripal_wide.c | $(BIN)
	$(CC) $(CFLAGS) $(RTLIB) -o $@ $<

# Fast consistency checks: decimal self-tests, cross-program agreement on the
# base 2 + base 16 palindrome list, and the incremental-residue verify mode.
check: all
	$(BIN)/tripal128 selftest
	$(BIN)/tripal_wide selftest
	$(BIN)/tripal_hex pairs 8 | grep '^[0-9]' | sort -n > $(BIN)/pairs_hex.txt
	$(BIN)/tripal128 pairs 8 | grep '^[0-9]' | sort -n > $(BIN)/pairs_128.txt
	$(BIN)/tripal_wide pairs 1 8 | grep '^[0-9]' | sort -n > $(BIN)/pairs_wide.txt
	cmp $(BIN)/pairs_hex.txt $(BIN)/pairs_128.txt
	cmp $(BIN)/pairs_hex.txt $(BIN)/pairs_wide.txt
	$(BIN)/tripal_wide verify 1 24 | grep -v '^[0-9]'

clean:
	rm -rf $(BIN)

.PHONY: all check clean
