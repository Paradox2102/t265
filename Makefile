CXX      := -g++
CXXFLAGS := -pedantic-errors -Wall -Wextra -std=gnu++11
LIBS     := -lstdc++ -lm -lrealsense2 -lntcore
LDFLAGS  := -L/usr/lib -L./lib 
BUILD    := ./build
OBJ_DIR  := $(BUILD)/objects
APP_DIR  := $(BUILD)/apps
TARGET   := program
INCLUDE  := -Iinclude/
SRC      := $(wildcard src/*.cpp)

OBJECTS := $(SRC:%.cpp=$(OBJ_DIR)/%.o)

all: build $(APP_DIR)/$(TARGET)

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -o $@ -c $<

$(APP_DIR)/$(TARGET): $(OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) $(LDFLAGS) -o $(APP_DIR)/$(TARGET) $(OBJECTS) $(LIBS)

.PHONY: all build clean debug release

build:
	@mkdir -p $(APP_DIR)
	@mkdir -p $(OBJ_DIR)

debug: CXXFLAGS += -DDEBUG -g
debug: all

release: CXXFLAGS += -O2
release: all

clean:
	-@rm -rvf $(OBJ_DIR)/*
	-@rm -rvf $(APP_DIR)/*
