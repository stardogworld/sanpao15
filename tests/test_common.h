#pragma once

#include <cstdlib>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sanpao15::test {

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

inline void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Fn>
void requireThrows(Fn&& fn, std::string_view message) {
    bool threw = false;
    try {
        fn();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

}  // namespace sanpao15::test

#define SANPAO15_TEST(name)                                      \
    void name();                                                 \
    namespace {                                                  \
    sanpao15::test::Registrar registrar_##name(#name, &name);    \
    }                                                            \
    void name()

#define SANPAO15_REQUIRE(condition) \
    sanpao15::test::require((condition), #condition)
