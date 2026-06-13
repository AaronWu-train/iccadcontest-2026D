cmake_minimum_required(VERSION 3.20)

project(cadd0040 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <filesystem>
int main() {
    std::filesystem::path path(\".\");
    return static_cast<int>(path.string().size());
}
" CXX_FILESYSTEM_NO_EXTRA_LINK)
if(NOT CXX_FILESYSTEM_NO_EXTRA_LINK)
    set(CXX_FILESYSTEM_LIBRARY stdc++fs)
endif()

include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)

FetchContent_MakeAvailable(Catch2 CLI11)

add_library(cadd0040_core
    src/app.cpp
    src/clock_tree.cpp
    src/datapath_graph.cpp
    src/debug_progress.cpp
    src/evaluation.cpp
    src/parser.cpp
    src/optimization/factory.cpp
    src/optimization/greedy/greedy_optimizer.cpp
    src/optimization/milp/milp_optimizer.cpp
    src/optimization/optimizer.cpp
    src/optimization/skew_model.cpp
    src/optimization/sa/annealing_optimizer.cpp
    src/optimization/sa/iterated_sa_optimizer.cpp
    src/optimization/sa/sa_common.cpp
    src/optimization/visual/clock_tree_trace_optimizer.cpp
    src/solver.cpp
)

target_include_directories(cadd0040_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(cadd0040_core
    PUBLIC
        CLI11::CLI11
)

if(CXX_FILESYSTEM_LIBRARY)
    target_link_libraries(cadd0040_core PUBLIC ${CXX_FILESYSTEM_LIBRARY})
endif()

target_compile_options(cadd0040_core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_compile_definitions(cadd0040_core PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
)

add_executable(cadd0040
    src/main.cpp
)

target_link_libraries(cadd0040
    PRIVATE
        cadd0040_core
)

include(CTest)
enable_testing()

add_subdirectory(tests)
