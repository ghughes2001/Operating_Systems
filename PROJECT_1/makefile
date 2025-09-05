# author: Grant Hughes

CXX: g++
CXXFLAGS: -Wall -ansi -pedantic -g -std=c++11

all: oss user

oss: oss.cpp
	$(CXX) $(CXXFLAGS) -o oss oss.cpp

user: user.cpp
	$(CXX) $(CXXFLAGS) -o user user.cpp

clean:
	rm -f oss user *.o

.PHONY: all clean