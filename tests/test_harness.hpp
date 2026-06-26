#pragma once

#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace test {

struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

using TestFn = void (*)();

struct TestCase {
    std::string name;
    TestFn fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(std::string name, TestFn fn) {
        registry().push_back({std::move(name), fn});
    }
};

template <class T, class U>
void expect_eq(const T& actual, const U& expected, const char* expr, const char* file, int line) {
    if (!(actual == expected)) {
        std::ostringstream oss;
        oss << file << ":" << line << " expected " << expr;
        throw Failure(oss.str());
    }
}

inline void expect_true(bool condition, const char* expr, const char* file, int line) {
    if (!condition) {
        std::ostringstream oss;
        oss << file << ":" << line << " expected true: " << expr;
        throw Failure(oss.str());
    }
}

inline void expect_near(
    double actual,
    double expected,
    double epsilon,
    const char* expr,
    const char* file,
    int line) {
    if (std::fabs(actual - expected) > epsilon) {
        std::ostringstream oss;
        oss << file << ":" << line << " expected " << expr << " within " << epsilon
            << " but got " << actual << " vs " << expected;
        throw Failure(oss.str());
    }
}

template <class Exception, class Fn>
void expect_throw(Fn&& fn, const char* expr, const char* file, int line) {
    try {
        fn();
    } catch (const Exception&) {
        return;
    } catch (const std::exception& ex) {
        std::ostringstream oss;
        oss << file << ":" << line << " expected " << expr << " to throw "
            << typeid(Exception).name() << " but got " << ex.what();
        throw Failure(oss.str());
    } catch (...) {
        std::ostringstream oss;
        oss << file << ":" << line << " expected " << expr << " to throw "
            << typeid(Exception).name();
        throw Failure(oss.str());
    }

    std::ostringstream oss;
    oss << file << ":" << line << " expected " << expr << " to throw "
        << typeid(Exception).name();
    throw Failure(oss.str());
}

}  // namespace test

#define TEST_CASE(name)                                                                      \
    static void name();                                                                      \
    static ::test::Registrar name##_registrar(#name, &name);                                \
    static void name()

#define EXPECT_TRUE(expr) ::test::expect_true((expr), #expr, __FILE__, __LINE__)
#define EXPECT_EQ(actual, expected) ::test::expect_eq((actual), (expected), #actual " == " #expected, __FILE__, __LINE__)
#define EXPECT_NEAR(actual, expected, epsilon) ::test::expect_near((actual), (expected), (epsilon), #actual " ~= " #expected, __FILE__, __LINE__)
#define EXPECT_THROW(expr, exception_type) ::test::expect_throw<exception_type>([&]() { (void)(expr); }, #expr, __FILE__, __LINE__)


