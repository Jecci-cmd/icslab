CXX = g++
CXXFLAGS = -std=c++17 -Wall -O2

TARGET = cpu
SRCS = cpu.cpp y86.cpp pipeline.cpp
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
