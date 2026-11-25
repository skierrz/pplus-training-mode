#pragma once

#include <types.h>

// The test runner: a few macros, no outside framework.
//
//   TEST(group, name)       { ... }   a test on an all-zero memory image
//   TEST_DUMP(group, name)  { ... }   a test on a memory dump of the running
//                                     game; skipped, saying so, without one
//   TEST_DUMP2(group, name) { ... }   the same, and the dump must have MEM2
//
//   CHECK(condition)                  goes on after a failure
//   CHECK_EQ(expected, found)         numbers of any kind; prints both
//   CHECK_NEAR(expected, found, tolerance)
//   CHECK_STR(expected, found)
//   CHECK_LOG(text)                   the mod logged a line containing text
//   REQUIRE(condition)                leaves the test on a failure
//   SKIP("why", ...)                  leaves the test as skipped
//   NOTE("text", ...)                 shown after the test's name
//   KNOWN_BUG("what is wrong", ...)   first line of a test that shows a bug
//                                     of the mod not fixed yet: its failure
//                                     is listed as "bug", not as a failure
//                                     of the run; once it passes, the run
//                                     fails until the mark is taken out
//
// A test that crashes (the mod's code reads or writes an address that is
// not mapped) fails with the address, and the run goes on.
//
// Before every test the arena is emptied, the mod's captured log is cleared
// and the fakes (host/fakes.h) are reset; the image is zeroed (TEST) or
// holds the dump (TEST_DUMP). The mod's own static state is NOT reset: a
// test calls the attach or reset function of what it tests first, as the
// game would.
namespace host {
namespace test {
    enum Needs { kNeedsNothing = 0, kNeedsDump = 1, kNeedsMem2 = 2 };

    struct Case {
        Case(const char* group, const char* name, void (*run)(), int needs);
        const char* group;
        const char* name;
        void (*run)();
        int needs;
        Case* next;
    };

    // A number of any kind, for CHECK_EQ.
    struct Value {
        Value(bool v) : real(false), i(v ? 1 : 0), d(0)
        {
        }
        Value(char v) : real(false), i(v), d(0)
        {
        }
        Value(signed char v) : real(false), i(v), d(0)
        {
        }
        Value(unsigned char v) : real(false), i(v), d(0)
        {
        }
        Value(short v) : real(false), i(v), d(0)
        {
        }
        Value(unsigned short v) : real(false), i(v), d(0)
        {
        }
        Value(int v) : real(false), i(v), d(0)
        {
        }
        Value(unsigned int v) : real(false), i(v), d(0)
        {
        }
        Value(long v) : real(false), i(v), d(0)
        {
        }
        Value(unsigned long v) : real(false), i(v), d(0)
        {
        }
        Value(long long v) : real(false), i(v), d(0)
        {
        }
        Value(unsigned long long v) : real(false), i(static_cast<long long>(v)), d(0)
        {
        }
        Value(float v) : real(true), i(0), d(v)
        {
        }
        Value(double v) : real(true), i(0), d(v)
        {
        }
        Value(const void* v) : real(false), i(reinterpret_cast<u32>(v)), d(0)
        {
        }
        bool real;
        long long i;
        double d;
    };

    void fail(const char* file, int line, const char* format, ...);
    void skip(const char* format, ...);
    void note(const char* format, ...);
    void knownBug(const char* format, ...);
    bool failing();
    void equal(const char* file, int line, const char* what, const Value& expected, const Value& found);
    void near_(const char* file, int line, const char* what, double expected, double found, double tolerance);
    void sameText(const char* file, int line, const char* what, const char* expected, const char* found);
    void logged(const char* file, int line, const char* text);

    // The dump given on the command line (--dump) or in TM_HOST_DUMP.
    bool haveDump();
    bool haveMem2();

    int runAll(int argc, char** argv);
}
}

#define HOST_TEST_(group, name, needs)                                                                                 \
    static void host_test_##group##_##name();                                                                          \
    static host::test::Case host_case_##group##_##name(#group, #name, host_test_##group##_##name, needs);             \
    static void host_test_##group##_##name()

#define TEST(group, name) HOST_TEST_(group, name, host::test::kNeedsNothing)
#define TEST_DUMP(group, name) HOST_TEST_(group, name, host::test::kNeedsDump)
#define TEST_DUMP2(group, name) HOST_TEST_(group, name, host::test::kNeedsDump | host::test::kNeedsMem2)

#define CHECK(condition)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            host::test::fail(__FILE__, __LINE__, "expected to hold: %s", #condition);                                  \
        }                                                                                                              \
    } while (0)

#define REQUIRE(condition)                                                                                             \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition))                                                                                              \
        {                                                                                                              \
            host::test::fail(__FILE__, __LINE__, "expected to hold: %s (the test stops here)", #condition);            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define CHECK_EQ(expected, found) host::test::equal(__FILE__, __LINE__, #found, (expected), (found))
#define CHECK_NEAR(expected, found, tolerance)                                                                         \
    host::test::near_(__FILE__, __LINE__, #found, (expected), (found), (tolerance))
#define CHECK_STR(expected, found) host::test::sameText(__FILE__, __LINE__, #found, (expected), (found))
#define CHECK_LOG(text) host::test::logged(__FILE__, __LINE__, (text))

#define SKIP(...)                                                                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        host::test::skip(__VA_ARGS__);                                                                                 \
        return;                                                                                                        \
    } while (0)

#define NOTE(...) host::test::note(__VA_ARGS__)
#define KNOWN_BUG(...) host::test::knownBug(__VA_ARGS__)
