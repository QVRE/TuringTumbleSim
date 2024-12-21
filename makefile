# Compiler and flags
CXX = g++
CXXFLAGS = -O2 -lncurses

# Source files and output binary
SRCS = main.cpp tumble.cpp
HEADERS = tumble.h
OBJS = $(SRCS:.cpp=.o)
TARGET = out

all: $(TARGET)

# Rule to build the target executable
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

# Rule to compile source files into object files
%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean rule to remove all binaries and objects
clean:
	rm $(OBJS) $(TARGET)
