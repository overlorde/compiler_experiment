CC      := gcc
CFLAGS  := -Wall -Wextra -g $(shell llvm-config-14 --cflags)
LDFLAGS := $(shell llvm-config-14 --ldflags --libs core irreader) $(shell llvm-config-14 --system-libs)

liveness: liveness.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f liveness

.PHONY: clean
