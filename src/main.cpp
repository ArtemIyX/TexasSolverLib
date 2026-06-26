#include "core/lib.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void print_usage() {
    std::cout << "TexasSolver C++ port\n"
              << "Usage:\n"
              << "  TexasSolver kuhn [iterations]\n"
              << "  TexasSolver leduc [iterations]\n"
              << "  TexasSolver help\n";
}

int parse_iterations(int argc, char** argv, int default_value) {
    if (argc < 3) {
        return default_value;
    }
    return std::atoi(argv[2]);
}

void print_solve_output(const core::SolveOutput& out) {
    std::cout << "iterations: " << out.iterations << '\n';
    std::cout << "game_value: " << out.game_value << '\n';
    std::cout << "exploitability: " << out.exploitability << '\n';
    std::cout << "average_strategy entries: " << out.average_strategy.size() << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    const std::string mode = argv[1];
    if (mode == "help" || mode == "--help" || mode == "-h") {
        print_usage();
        return 0;
    }

    if (mode == "kuhn") {
        const auto iterations = static_cast<std::uint32_t>(parse_iterations(argc, argv, 1000));
        const auto out = core::lib::solve_kuhn(iterations, 1.5, 0.0, 2.0);
        print_solve_output(out);
        return 0;
    }

    if (mode == "leduc") {
        const auto iterations = static_cast<std::uint32_t>(parse_iterations(argc, argv, 1000));
        const auto out = core::lib::solve_leduc(iterations, 1.5, 0.0, 2.0);
        print_solve_output(out);
        return 0;
    }

    std::cerr << "Unknown command: " << mode << '\n';
    print_usage();
    return 1;
}
