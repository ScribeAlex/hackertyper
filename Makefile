# hackertyper -- single translation unit, no dependencies.

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -pedantic
PREFIX  ?= /usr/local

TARGET  := hackertyper
SRC     := hackertyper.c
TEST    := test_hackertyper
TESTSRC := test_hackertyper.c

.PHONY: all run test clean install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

# The test includes hackertyper.c directly, so it depends on both sources.
test: $(TEST)
	./$(TEST)

$(TEST): $(TESTSRC) $(SRC)
	$(CC) $(CFLAGS) -o $@ $(TESTSRC)

install: $(TARGET)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe $(TARGET).obj
	rm -f $(TEST) $(TEST).exe $(TEST).obj
