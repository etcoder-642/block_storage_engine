CXX = g++
CXXFLAGS = -std=c++17 -Wall -Iinclude
TARGET = main
SRCS = src/main.cpp src/api.cpp

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)