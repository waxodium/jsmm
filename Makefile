.PHONY: all init build rebuild clean distclean

BUILD_DIR := build
CMAKE_DIR := $(BUILD_DIR)/cmake
BUILD_TYPE ?= Release
JOBS ?= $(shell nproc 2>/dev/null || echo 1)

all: build

init:
	@if [ ! -f "$(CMAKE_DIR)/CMakeCache.txt" ]; then \
		cmake -S . -B $(CMAKE_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE); \
	fi

build: init
	cmake --build $(CMAKE_DIR) --parallel $(JOBS)

rebuild: distclean build

clean:
	cmake --build $(CMAKE_DIR) --target clean 2>/dev/null || rm -rf $(CMAKE_DIR)
	rm -f $(BUILD_DIR)/jsd

distclean:
	rm -rf $(BUILD_DIR)
