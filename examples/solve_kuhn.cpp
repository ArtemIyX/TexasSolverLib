#include "solver/generic/solver.hpp"

#include <iostream>

int main() {
    const auto output = texas::solve_kuhn(10, 1.5, 0.0, 2.0);
    std::cout << "iterations=" << output.iterations << "\n";
    std::cout << "game_value=" << output.game_value << "\n";
    std::cout << "exploitability=" << output.exploitability << "\n";
    return 0;
}

