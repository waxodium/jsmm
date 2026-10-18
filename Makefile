.PHONY: all init build clean

all: build

init:
	@if [ ! -d "build" ]; then \
		cmake -S . -B build/cmake; \
	fi

build: init
	cmake --build build/cmake --parallel $$(nproc)

clean:
	rm -rf build/

