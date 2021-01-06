CC = g++-10
SRC = builtins.cpp utils.cpp main.cpp
BIN = wsh

all:
	$(CC) --std=c++20 $(SRC) -o $(BIN)

install:
	cp $(BIN) ~/bin/$(BIN)
