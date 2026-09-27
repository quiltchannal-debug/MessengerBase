# OrangeM server
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -pthread -I src
LDFLAGS  ?= -pthread -lsqlite3 -lssl -lcrypto

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
BIN := orangem

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.cpp $(wildcard src/*.h)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

run: $(BIN)
	./$(BIN) --port 8080 --data ./data --web ./web

.PHONY: all clean run
