CC = g++
CFLAGS = -Wall -g
TARGET1 = oss
TARGET2 = user
OBJS1 = oss.o
OBJS2 = user.o

.SUFFIXES: .cpp .o

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(OBJS1)
	$(CC) $(CFLAGS) -o $(TARGET1) $(OBJS1)

$(TARGET2): $(OBJS2)
	$(CC) $(CFLAGS) -o $(TARGET2) $(OBJS2)

.cpp.o:
	$(CC) $(CFLAGS) -c $<

oss.o: oss.cpp

user.o: user.cpp

clean:
	/bin/rm -f $(TARGET1) $(TARGET2) *.o

cleanipc:
	@echo "Cleaning up IPC resources..."
	@-ipcrm -a 2>/dev/null || true
	@echo "IPC cleanup complete"

cleanall: clean
	/bin/rm -f *~