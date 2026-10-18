CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Werror -O2
SRC_DIR := src
BUILD_DIR := build
TARGET := $(BUILD_DIR)/rma

SOURCES := $(sort $(wildcard $(SRC_DIR)/*.cc $(SRC_DIR)/*/*.cc $(SRC_DIR)/*/*/*.cc))
HEADERS := $(sort $(wildcard $(SRC_DIR)/*.hh $(SRC_DIR)/*/*.hh $(SRC_DIR)/*/*/*.hh))

.PHONY: all
all: $(TARGET)

$(TARGET): $(SOURCES) $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) $(SOURCES) -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

