#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "string-ref.hpp"

namespace {

using namespace std::string_view_literals;

#define CHECK_TRUE(EXPR) test.check_true(__LINE__, #EXPR, (EXPR))
#define CHECK_EQ(LHS, RHS) test.check_eq(__LINE__, #LHS, #RHS, (LHS), (RHS))
#define CHECK_NE(LHS, RHS) test.check_ne(__LINE__, #LHS, #RHS, (LHS), (RHS))

class TestCtx {
public:
    void check_true(int line, const char *expr, bool value) {
        if (!value) {
            std::cerr << "  assertion failed at line " << line << ": " << expr << '\n';
            failed_ = true;
        }
    }

    template<class T, class U>
    void check_eq(
        int line,
        const char *lhs_expr,
        const char *rhs_expr,
        const T &lhs,
        const U &rhs
    ) {
        if (lhs != rhs) {
            std::cerr << "  assertion failed at line " << line << ": " << lhs_expr
                      << " == " << rhs_expr << '\n';
            std::cerr << "    lhs: " << lhs << '\n';
            std::cerr << "    rhs: " << rhs << '\n';
            failed_ = true;
        }
    }

    template<class T>
    void check_ne(
        int line,
        const char *lhs_expr,
        const char *rhs_expr,
        const T &lhs,
        const std::type_identity_t<T> &rhs
    ) {
        if (lhs == rhs) {
            std::cerr << "  assertion failed at line " << line << ": " << lhs_expr
                      << " != " << rhs_expr << '\n';

            if constexpr (std::is_pointer_v<T>) {
                const void *lhs_p = lhs;
                const void *rhs_p = rhs;
                std::cerr << "    lhs: " << lhs_p << '\n';
                std::cerr << "    rhs: " << rhs_p << '\n';
            } else {
                std::cerr << "    lhs: " << lhs << '\n';
                std::cerr << "    rhs: " << rhs << '\n';
            }

            failed_ = true;
        }
    }

    bool failed() const noexcept {
        return failed_;
    }

private:
    bool failed_ = false;
};

class TestCase {
public:
    using Sig = void(TestCtx &);

private:
    TestCase(std::string name, std::function<Sig> func)
        : name_(std::move(name)), func_(std::move(func)) {}

    std::string name_;
    std::function<Sig> func_;

    friend class TestRunner;
};

class TestRunner {
public:
    static struct Tests {
        Tests &add(std::string name, std::function<TestCase::Sig> func) {
            test_cases.push_back(TestCase(std::move(name), std::move(func)));

            return *this;
        }

        bool finish() const noexcept {
            return true;
        }
    } tests;

    static bool run() {
        size_t successes = 0;
        std::vector<const TestCase *> failures;

        for (auto &test_case : test_cases) {
            std::cerr << "running " << test_case.name_ << "\n";
            TestCtx ctx;
            test_case.func_(ctx);

            if (ctx.failed()) {
                failures.push_back(&test_case);
            } else {
                ++successes;
            }
        }

        std::cerr << "\n";

        if (!failures.empty()) {
            std::cerr << "failed tests:\n";

            for (const auto *test_case : failures) {
                std::cerr << "  " << test_case->name_ << "\n";
            }

            std::cerr << "\n";
        }

        std::cerr << "ok: " << successes << "; failed: " << failures.size() << "\n";

        return failures.empty();
    }

private:
    TestRunner() = default;

    static std::vector<TestCase> test_cases;
};

TestRunner::Tests TestRunner::tests;
std::vector<TestCase> TestRunner::test_cases;

template<class T>
void bubble_sort(std::vector<T> &elems) {
    for (size_t n = elems.size();; --n) {
        bool swapped = false;

        for (size_t i = 0; i + 1 < n; ++i) {
            if (elems[i] > elems[i + 1]) {
                std::swap(elems[i], elems[i + 1]);
                swapped = true;
            }
        }

        if (!swapped) {
            return;
        }
    }
}

[[maybe_unused]]
bool _ = TestRunner::tests
             .add(
                 "init: default",
                 [](auto &test) {
                     StringRef ref;

                     CHECK_TRUE(!ref.unique());
                     CHECK_TRUE(ref.shared());
                     CHECK_EQ(ref, ""sv);
                 }
             )

             .add(
                 "init: std::string",
                 [](auto &test) {
                     std::string str = "hello, world";
                     StringRef ref(str);

                     CHECK_EQ(ref, str);
                     CHECK_TRUE(ref.unique());
                     CHECK_TRUE(!ref.shared());
                 }
             )

             .add(
                 "init: std::string_view",
                 [](auto &test) {
                     std::string_view str = "hello, world";
                     str = str.substr(0, 5);
                     StringRef ref(str);

                     CHECK_EQ(ref, str);
                     CHECK_TRUE(ref.unique());
                     CHECK_TRUE(!ref.shared());
                 }
             )

             .add(
                 "init: char *",
                 [](auto &test) {
                     const char str[] = "hello, world";
                     StringRef ref(str);

                     CHECK_EQ(ref, std::string_view(str));
                     CHECK_TRUE(ref.unique());
                     CHECK_TRUE(!ref.shared());
                 }
             )

             .add(
                 "init: char *, size_t",
                 [](auto &test) {
                     const char str[] = "hello, world";
                     StringRef ref(str, 5);

                     CHECK_EQ(ref, "hello"sv);
                     CHECK_TRUE(ref.unique());
                     CHECK_TRUE(!ref.shared());
                 }
             )

             .add(
                 "init: copy",
                 [](auto &test) {
                     StringRef ref("hello, world");
                     // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
                     auto copy = ref;

                     CHECK_EQ(ref.get(), copy.get());
                     CHECK_TRUE(!ref.unique());
                     CHECK_TRUE(!copy.unique());
                     CHECK_TRUE(ref.shared());
                     CHECK_TRUE(copy.shared());
                 }
             )

             .add(
                 "assign: copy",
                 [](auto &test) {
                     StringRef a("hello");
                     StringRef b("world");
                     b = a;

                     CHECK_EQ(a.get(), b.get());
                     CHECK_TRUE(!a.unique());
                     CHECK_TRUE(!b.unique());
                     CHECK_TRUE(a.shared());
                     CHECK_TRUE(b.shared());
                 }
             )

             .add(
                 "init: move",
                 [](auto &test) {
                     std::string_view hello = "hello, world";
                     StringRef a(hello);
                     StringRef b(std::move(a));

                     CHECK_EQ(b, hello);
                     CHECK_TRUE(b.unique());
                     CHECK_TRUE(!b.shared());
                 }
             )

             .add(
                 "assign: move",
                 [](auto &test) {
                     std::string_view hello = "hello, world";

                     StringRef ref;

                     {
                         StringRef hello_ref(hello);
                         ref = std::move(hello_ref);
                     }

                     CHECK_EQ(ref, hello);
                     CHECK_TRUE(ref.unique());
                     CHECK_TRUE(!ref.shared());
                 }
             )

             .add(
                 "self-assign: copy",
                 [](auto &test) {
                     std::string_view hello = "hello";
                     StringRef ref(hello);
                     ref = ref;

                     CHECK_EQ(ref, hello);
                     CHECK_TRUE(ref.unique());
                     CHECK_TRUE(!ref.shared());
                 }
             )

             .add(
                 "self-assign: move",
                 [](auto &test) {
                     std::string_view hello = "hello";
                     StringRef ref(hello);
                     ref = std::move(ref);

                     CHECK_EQ(ref, hello);
                     CHECK_TRUE(ref.unique());
                     CHECK_TRUE(!ref.shared());
                 }
             )

             .add(
                 "assign: string",
                 [](auto &test) {
                     StringRef ref = "hello";
                     ref = "world";

                     CHECK_EQ(ref, "world");
                     CHECK_TRUE(ref.unique());
                     CHECK_TRUE(!ref.shared());
                 }
             )

             .add(
                 "source mutation",
                 [](auto &test) {
                     std::string_view hello = "hello";
                     std::string str(hello);
                     StringRef ref(str);
                     str[0] = '1';
                     str[1] = '2';
                     str[2] = '3';
                     str[3] = '4';
                     str[4] = '5';

                     CHECK_EQ(ref, hello);
                 }
             )

             .add(
                 "comparisons",
                 [](auto &test) {
                     StringRef hello("hello");
                     StringRef world("world");

                     CHECK_TRUE(hello == hello);
                     CHECK_TRUE(hello <= "hello, world");
                     CHECK_TRUE(!(hello < "hello"));
                     CHECK_TRUE("hello, world" > hello);
                     CHECK_TRUE(hello != world);
                     CHECK_TRUE(hello < world);
                     CHECK_TRUE(!(hello >= world));
                 }
             )

             .add(
                 "bubble sort",
                 [](auto &test) {
                     StringRef shared("shared");
                     std::vector<StringRef> refs;
                     refs.emplace_back("lorem");
                     refs.emplace_back("ipsum");
                     refs.emplace_back("dolor");
                     refs.emplace_back(shared);
                     refs.emplace_back("sit");
                     refs.emplace_back("amet");

                     bubble_sort(refs);

                     CHECK_EQ(refs[0], "amet");
                     CHECK_TRUE(refs[0].unique());

                     CHECK_EQ(refs[1], "dolor");
                     CHECK_TRUE(refs[1].unique());

                     CHECK_EQ(refs[2], "ipsum");
                     CHECK_TRUE(refs[2].unique());

                     CHECK_EQ(refs[3], "lorem");
                     CHECK_TRUE(refs[3].unique());

                     CHECK_EQ(refs[4], "shared");
                     CHECK_TRUE(refs[4].shared());

                     CHECK_EQ(refs[5], "sit");
                     CHECK_TRUE(refs[5].unique());
                 }
             )

             .finish();

} // namespace

int main() {
    return TestRunner::run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
