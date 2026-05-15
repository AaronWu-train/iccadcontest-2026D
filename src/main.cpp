#include <CLI/CLI.hpp>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "app.hpp"

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);

    try {
        const auto config = cadd0040::parse_arguments(args);
        return cadd0040::run(config);
    } catch (const CLI::CallForHelp&) {
        std::cout << cadd0040::help_message();
        return 0;
    } catch (const CLI::ParseError& error) {
        std::cerr << error.what() << '\n';
        return error.get_exit_code();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
