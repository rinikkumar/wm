CC = clang
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lxcb

TARGETS = wm wmc
OBJS = wm.o wmc.o utils.o ipc.o

.PHONY: all clean format

all: $(TARGETS)

wm: wm.o utils.o ipc.o
	$(CC) -o $@ $^ $(LDFLAGS)

wmc: wmc.o utils.o ipc.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS) $(OBJS)

format:
	clang-format -style=Mozilla -i *.c *.h
