.PHONY: all configure build release test run format clean

all: build

BUILD_DIR := build
RELEASE_DIR := build-release
TARGET := cadd0040

ifeq ($(shell command -v ninja >/dev/null 2>&1 && echo yes),yes)
CMAKE_GENERATOR := -G Ninja
else
CMAKE_GENERATOR :=
endif

JOBS ?= $(shell nproc)

configure:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_GENERATOR) -DCMAKE_BUILD_TYPE=Debug

build:
	cmake -S . -B $(BUILD_DIR) $(CMAKE_GENERATOR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) --parallel $(JOBS)

release:
	cmake -S . -B $(RELEASE_DIR) $(CMAKE_GENERATOR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(RELEASE_DIR) --parallel $(JOBS)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

run: build
	./scripts/run_all_testcases.sh --build-dir $(BUILD_DIR)

format:
	clang-format -i src/*.cpp src/*.hpp tests/*.cpp

clean:
	rm -rf $(BUILD_DIR) $(RELEASE_DIR)
