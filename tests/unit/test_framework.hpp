#pragma once

// Minimal, dependency-free test harness for chronicle-core's v0.1 unit
// tests. Deliberately not GoogleTest/Catch2: docs/11-repository-structure-
// and-standards.md commits to those as the project matures, but pulling
// them in via FetchContent requires network access this scaffold shouldn't
// assume. Swapping this out is a CMakeLists.txt + include change only --
// no test file should need to change beyond CHECK/CHECK_EQ call sites,
// which is why this mimics that shape rather than inventing a new one.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace chronicle::test {

struct Failure {
    std::string test_name;
    std::string message;
};

inline std::vector<Failure>& failures() {
    static std::vector<Failure> instance;
    return instance;
}

inline int& check_count() {
    static int instance = 0;
    return instance;
}

inline void record_check(bool ok, std::string const& test_name, std::string const& expr,
                          std::string const& file, int line) {
    ++check_count();
    if (!ok) {
        std::ostringstream oss;
        oss << file << ":" << line << ": CHECK failed: " << expr;
        failures().push_back({test_name, oss.str()});
    }
}

using TestFn = void (*)();

struct Registration {
    Registration(char const* name, TestFn fn);
};

inline std::vector<std::pair<std::string, TestFn>>& registry() {
    static std::vector<std::pair<std::string, TestFn>> instance;
    return instance;
}

inline Registration::Registration(char const* name, TestFn fn) {
    registry().emplace_back(name, fn);
}

inline int run_all() {
    for (auto const& [name, fn] : registry()) {
        fn();
    }
    for (auto const& failure : failures()) {
        std::cerr << "[FAIL] " << failure.test_name << " -- " << failure.message << "\n";
    }
    std::cout << (check_count() - static_cast<int>(failures().size())) << "/" << check_count()
              << " checks passed across " << registry().size() << " test(s)\n";
    return failures().empty() ? 0 : 1;
}

} // namespace chronicle::test

#define CHRONICLE_TEST(name)                                                                     \
    void name();                                                                                  \
    static chronicle::test::Registration chronicle_test_reg_##name(#name, &name);                 \
    void name()

#define CHRONICLE_CHECK(expr)                                                                     \
    chronicle::test::record_check(static_cast<bool>(expr), __func__, #expr, __FILE__, __LINE__)
