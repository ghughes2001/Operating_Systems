CC = g++
CFLAGS = -Wall -g
TARGETS = oss user

.PHONY: all clean

all: $(TARGETS)

oss: oss.cpp shared.h
	$(CC) $(CFLAGS) -o oss oss.cpp

user: user.cpp shared.h
	$(CC) $(CFLAGS) -o user user.cpp

clean:
	rm -f $(TARGETS) *.o log.txt
	rm -rf *.dSYM
	ipcrm -a 2>/dev/null || true