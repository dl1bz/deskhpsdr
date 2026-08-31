CC      := clang
CFLAGS  := -Wall -Wextra -Wpedantic -O2
TARGET  := p2_tool
SRC     := p2_tool.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/bin/$(TARGET)
