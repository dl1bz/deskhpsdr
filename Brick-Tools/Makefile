CFLAGS  := -Wall -Wextra -Wpedantic -O2
SRC     := p2_tool.c

ifeq ($(OS),Windows_NT)
CC      := gcc
TARGET  := p2_tool.exe
LDLIBS  := -lws2_32 -liphlpapi
else
CC      := clang
TARGET  := p2_tool
LDLIBS  :=
endif

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
ifeq ($(OS),Windows_NT)
	-del /Q $(TARGET) 2>NUL
else
	rm -f $(TARGET)
endif

ifeq ($(OS),Windows_NT)
install:
	@echo "No install target for Windows."
else
install: $(TARGET)
	install -m 0755 $(TARGET) /usr/local/bin/$(TARGET)
endif
