#include "test_harness.hpp"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    int failed = 0;
    std::size_t executed = 0;
    for (const auto& test_case : test::registry()) {
        if (argc > 1 &&
            test_case.name.find(argv[1]) == std::string::npos) {
            continue;
        }
        ++executed;
        std::cout << "[RUN] " << test_case.name << std::endl;
        try {
            test_case.fn();
            std::cout << "[PASS] " << test_case.name << '\n';
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "[FAIL] " << test_case.name << ": " << ex.what() << '\n';
        } catch (...) {
            ++failed;
            std::cerr << "[FAIL] " << test_case.name << ": unknown exception\n";
        }
    }

    if (failed != 0) {
        std::cerr << failed << " test(s) failed\n";
        return 1;
    }

    std::cout << "All tests passed (" << executed << ")\n";
    return 0;
}


