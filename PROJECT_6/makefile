CC = g++
CFLAGS = -Wall -Wextra -g -std=c++11
TARGETS = oss user

all: $(TARGETS)

oss: oss.cpp shared.h
	$(CC) $(CFLAGS) -o oss oss.cpp

user: user.cpp shared.h
	$(CC) $(CFLAGS) -o user user.cpp

clean:
	rm -f $(TARGETS) *.o logfile.txt
	rm -rf *.dSYM
	ipcrm -a 2>/dev/null || true

.PHONY: all clean