# Host build for the RAOP volume transfer-function unit test.
#
#   make -C test && ./test/test_raop_volume
#
# The test is deliberately not part of the idf.py build: it exercises
# raop_volume.c, which is pure arithmetic with no ESP-IDF dependencies.

CC       ?= cc
CFLAGS   ?= -std=c11 -O1 -Wall -Wextra
RAOP_SRC := ../components/esp-raop-receiver/src

test_raop_volume: test_raop_volume.c $(RAOP_SRC)/raop_volume.c $(RAOP_SRC)/raop_volume.h
	$(CC) $(CFLAGS) -I $(RAOP_SRC) $(filter %.c,$^) -lm -o $@

.PHONY: clean
clean:
	rm -f test_raop_volume
