/**
 * @file main.cpp
 * @brief Executable entry point for the ICCAD Contest 2026 Problem D solver.
 */

#include <exception>
#include <iostream>

#include "app.hpp"

int main(int argc, char** argv) {
    try {
        const auto config = cadd0040::parse_arguments(argc, argv);
        return cadd0040::run(config);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
