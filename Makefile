CC = clang
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lxcb

TARGET = wm
SRC = wm.c
OBJ = $(SRC:.c=.o)
HEADERS = config.h

.PHONY: all clean format

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJ)

format:
	clang-format -style=Mozilla -i $(SRC) $(HEADERS)