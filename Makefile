CXX:=clang++
CXXFLAGS:=-std=c++17 -g -Wall

SRC_DIR:=src
INC_DIR:=include
BIN_DIR:=bin
OBJ_DIR:=obj
TEST_DIR:=test
SRCS:=$(wildcard $(SRC_DIR)/*.cpp)
OBJS:=$(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.cpp=.o)))
INCLUDE:=-I$(INC_DIR)
DUMMY:=$(shell mkdir -p $(BIN_DIR) $(OBJ_DIR))

all: hnet server

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDE) -o $@ -c $^

hnet: $(OBJS)
	ar rcs $(BIN_DIR)/libhnet.a $(OBJS)

server: hnet
	$(CXX) $(CXXFLAGS) $(INCLUDE) -Lbin -lhnet -o $(TEST_DIR)/$@ $(TEST_DIR)/server.cpp 

clean:
	rm -f $(BIN_DIR)/*.a $(OBJ_DIR)/*.o $(TEST_DIR)/server
	rm -rf $(TEST_DIR)/server.d*

.PHONY: all test clean