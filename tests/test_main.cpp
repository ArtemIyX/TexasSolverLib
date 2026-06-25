#include "test_harness.hpp"

#include <exception>
#include <iostream>

int main() {
    int failed = 0;
    for (const auto& test_case : test::registry()) {
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

    std::cout << "All tests passed (" << test::registry().size() << ")\n";
    return 0;
}
