# Compiler and flags
CXX = g++
CXXFLAGS = -Wall -std=c++20 -D_GNU_SOURCE
LDFLAGS = -lcurl -ljsoncpp -lstdc++fs

# Target and sources
TARGET = ./build/lyric_buddy
SOURCES = ./src/lyric_buddy.cpp

# Build rules
all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)
