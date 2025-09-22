CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -g -std=c++11
TARGET1 = oss
TARGET2 = worker
SOURCES1 = oss.cpp
SOURCES2 = worker.cpp
OBJECTS1 = $(SOURCES1:.cpp=.o)
OBJECTS2 = $(SOURCES2:.cpp=.o)

.PHONY: all clean

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(OBJECTS1)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(TARGET2): $(OBJECTS2)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS1) $(OBJECTS2) $(TARGET1) $(TARGET2)

install: all

uninstall:
	rm -f $(TARGET1) $(TARGET2)