#include "host/test.h"

#include "host/fakes.h"
#include "host/log.h"
#include "host/mem.h"

#undef free
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace host {
namespace log {
    enum { kCap = 1 << 16 };
    static char s_text[kCap];
    static u32 s_used = 0;
    bool g_echo = false;

    void clear()
    {
        s_used = 0;
        s_text[0] = '\0';
    }

    void write(const char* format, va_list args)
    {
        char line[1024];
        vsnprintf(line, sizeof(line), format, args);
        if (g_echo)
        {
            printf("     | %s", line);
        }
        u32 n = static_cast<u32>(strlen(line));
        if (s_used + n + 1 > kCap)
        {
            // Keep the newer half.
            memmove(s_text, s_text + kCap / 2, s_used - kCap / 2);
            s_used -= kCap / 2;
        }
        if (n + 1 <= kCap - s_used)
        {
            memcpy(s_text + s_used, line, n + 1);
            s_used += n;
        }
    }

    const char* text()
    {
        return s_text;
    }

    int count(const char* part)
    {
        int found = 0;
        for (const char* line = s_text; *line != '\0';)
        {
            const char* end = strchr(line, '\n');
            u32 n = end != NULL ? static_cast<u32>(end - line) : static_cast<u32>(strlen(line));
            const char* at = strstr(line, part);
            if (at != NULL && at < line + n)
            {
                found++;
            }
            line += n + (end != NULL ? 1 : 0);
        }
        return found;
    }
}

namespace test {
    static Case* s_first = NULL;
    static Case* s_last = NULL;

    static const char* s_dumpMem1 = NULL;
    static char s_dumpMem2[1024];
    static bool s_haveMem2 = false;
    static bool s_dumpLoaded = false;

    // The test being run.
    static int s_failures = 0;
    static bool s_skipped = false;
    static bool s_knownBug = false;
    static char s_bug[256];
    static char s_note[256];
    static char s_details[8192];
    static u32 s_detailsUsed = 0;

    Case::Case(const char* g, const char* n, void (*r)(), int needed) : group(g), name(n), run(r), needs(needed), next(NULL)
    {
        // In the order the files were linked, each file's tests in the
        // order they are written.
        if (s_last != NULL)
        {
            s_last->next = this;
        }
        else
        {
            s_first = this;
        }
        s_last = this;
    }

    static void detail(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        int n = vsnprintf(s_details + s_detailsUsed, sizeof(s_details) - s_detailsUsed, format, args);
        va_end(args);
        if (n > 0)
        {
            s_detailsUsed += static_cast<u32>(n);
            if (s_detailsUsed >= sizeof(s_details))
            {
                s_detailsUsed = sizeof(s_details) - 1;
            }
        }
    }

    // The path as it is in the repository (from tests/host/ or src/ on),
    // with forward slashes: the compiler gives the PC's full path.
    static const char* shortPath(const char* file)
    {
        static char out[260];
        snprintf(out, sizeof(out), "%s", file);
        for (char* p = out; *p != '\0'; p++)
        {
            *p = *p == '\\' ? '/' : *p;
        }
        const char* tests = strstr(out, "tests/host/");
        if (tests != NULL)
        {
            return tests;
        }
        const char* best = out;
        for (const char* p = out; *p != '\0'; p++)
        {
            if ((p == out || p[-1] == '/') && strncmp(p, "src/", 4) == 0)
            {
                best = p;
            }
        }
        return best;
    }

    static void where(const char* file, int line)
    {
        s_failures++;
        detail("       %s:%d: ", shortPath(file), line);
    }

    void fail(const char* file, int line, const char* format, ...)
    {
        char text[512];
        va_list args;
        va_start(args, format);
        vsnprintf(text, sizeof(text), format, args);
        va_end(args);
        where(file, line);
        detail("%s\n", text);
    }

    void skip(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(s_note, sizeof(s_note), format, args);
        va_end(args);
        s_skipped = true;
    }

    void note(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(s_note, sizeof(s_note), format, args);
        va_end(args);
    }

    void knownBug(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(s_bug, sizeof(s_bug), format, args);
        va_end(args);
        s_knownBug = true;
    }

    bool failing()
    {
        return s_failures > 0;
    }

    // Runs the test; a fault of the processor inside it (an address that is
    // not mapped, a division by zero) comes back as its code and address.
    static bool guarded(void (*run)(), u32& code, u32& address)
    {
        __try
        {
            run();
            return true;
        }
        __except (code = GetExceptionCode(),
                  address = static_cast<u32>(GetExceptionInformation()->ExceptionRecord->NumberParameters >= 2
                                                 ? GetExceptionInformation()->ExceptionRecord->ExceptionInformation[1]
                                                 : 0),
                  EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void print(char* out, u32 cap, const Value& v)
    {
        if (v.real)
        {
            snprintf(out, cap, "%.9g", v.d);
        }
        else if (v.i > 9 || v.i < -9)
        {
            snprintf(out, cap, "%lld (0x%llx)", v.i, v.i & 0xFFFFFFFFull);
        }
        else
        {
            snprintf(out, cap, "%lld", v.i);
        }
    }

    void equal(const char* file, int line, const char* what, const Value& expected, const Value& found)
    {
        bool same = (expected.real || found.real)
                        ? (expected.real ? expected.d : static_cast<double>(expected.i))
                              == (found.real ? found.d : static_cast<double>(found.i))
                        : expected.i == found.i;
        if (same)
        {
            return;
        }
        char e[64], f[64];
        print(e, sizeof(e), expected);
        print(f, sizeof(f), found);
        where(file, line);
        detail("%s: expected %s, found %s\n", what, e, f);
    }

    void near_(const char* file, int line, const char* what, double expected, double found, double tolerance)
    {
        double off = expected > found ? expected - found : found - expected;
        if (off <= tolerance)
        {
            return;
        }
        where(file, line);
        detail("%s: expected %.9g (within %g), found %.9g\n", what, expected, tolerance, found);
    }

    void sameText(const char* file, int line, const char* what, const char* expected, const char* found)
    {
        if (found != NULL && strcmp(expected, found) == 0)
        {
            return;
        }
        where(file, line);
        detail("%s: expected \"%s\", found %s%s%s\n", what, expected, found != NULL ? "\"" : "",
               found != NULL ? found : "no text", found != NULL ? "\"" : "");
    }

    void logged(const char* file, int line, const char* text)
    {
        if (log::contains(text))
        {
            return;
        }
        where(file, line);
        detail("expected a log line holding \"%s\"; the mod logged:\n", text);
        const char* all = log::text();
        if (*all == '\0')
        {
            detail("         (nothing)\n");
        }
        for (const char* p = all; *p != '\0';)
        {
            const char* end = strchr(p, '\n');
            int n = end != NULL ? static_cast<int>(end - p) : static_cast<int>(strlen(p));
            detail("         %.*s\n", n, p);
            p += n + (end != NULL ? 1 : 0);
        }
    }

    bool haveDump()
    {
        return s_dumpMem1 != NULL;
    }

    bool haveMem2()
    {
        return s_haveMem2;
    }

    static bool exists(const char* path)
    {
        FILE* f = fopen(path, "rb");
        if (f != NULL)
        {
            fclose(f);
        }
        return f != NULL;
    }

    // `given` is a dump's prefix (PREFIX.mem1 and PREFIX.mem2, as
    // scripts/memdump.py writes them), its .mem1 file, or a file holding
    // MEM1 alone.
    static bool findDump(const char* given)
    {
        static char mem1[1024];
        u32 n = static_cast<u32>(strlen(given));
        if (n + 8 > sizeof(mem1))
        {
            return false;
        }
        snprintf(mem1, sizeof(mem1), "%s.mem1", given);
        if (exists(mem1))
        {
            snprintf(s_dumpMem2, sizeof(s_dumpMem2), "%s.mem2", given);
        }
        else if (exists(given))
        {
            snprintf(mem1, sizeof(mem1), "%s", given);
            s_dumpMem2[0] = '\0';
            if (n > 5 && strcmp(given + n - 5, ".mem1") == 0)
            {
                snprintf(s_dumpMem2, sizeof(s_dumpMem2), "%.*s.mem2", static_cast<int>(n - 5), given);
            }
        }
        else
        {
            return false;
        }
        s_dumpMem1 = mem1;
        s_haveMem2 = s_dumpMem2[0] != '\0' && exists(s_dumpMem2);
        return true;
    }

    static double seconds()
    {
        LARGE_INTEGER now, perSecond;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&perSecond);
        return static_cast<double>(now.QuadPart) / static_cast<double>(perSecond.QuadPart);
    }

    static bool prepare(const Case& c)
    {
        if ((c.needs & kNeedsDump) == 0)
        {
            mem::clear();
            s_dumpLoaded = false;
            return true;
        }
        if (!haveDump())
        {
            skip("needs a memory dump of the running game: pass --dump or set TM_HOST_DUMP (docs/testing.md)");
            return false;
        }
        if ((c.needs & kNeedsMem2) != 0 && !haveMem2())
        {
            skip("needs a dump with MEM2 (PREFIX.mem1 and PREFIX.mem2); the one given holds MEM1 alone");
            return false;
        }
        if (s_dumpLoaded)
        {
            // Dump tests only read the image, so one load serves them all.
            mem::clearArena();
            return true;
        }
        char why[1200];
        if (!mem::loadDump(s_dumpMem1, s_haveMem2 ? s_dumpMem2 : NULL, why, sizeof(why)))
        {
            fail(__FILE__, __LINE__, "the dump could not be loaded: %s", why);
            return false;
        }
        s_dumpLoaded = true;
        return true;
    }

    int runAll(int argc, char** argv)
    {
        const char* only = NULL;
        const char* dump = getenv("TM_HOST_DUMP");
        bool list = false;
        for (int i = 1; i < argc; i++)
        {
            if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc)
            {
                dump = argv[++i];
            }
            else if (strcmp(argv[i], "--only") == 0 && i + 1 < argc)
            {
                only = argv[++i];
            }
            else if (strcmp(argv[i], "--log") == 0)
            {
                log::g_echo = true;
            }
            else if (strcmp(argv[i], "--list") == 0)
            {
                list = true;
            }
            else
            {
                printf("usage: host_tests [--dump PREFIX-OR-FILE] [--only TEXT] [--log] [--list]\n"
                       "  --dump  a memory dump of the running game: PREFIX of PREFIX.mem1 and PREFIX.mem2,\n"
                       "          or a file holding MEM1 alone (also taken from TM_HOST_DUMP)\n"
                       "  --only  run the tests whose group.name holds TEXT\n"
                       "  --log   print what the mod logs\n"
                       "  --list  print the tests' names\n");
                return 2;
            }
        }
        if (dump != NULL && *dump != '\0' && !findDump(dump))
        {
            printf("host: no dump at %s (looked for that file and for %s.mem1)\n", dump, dump);
            return 2;
        }
        if (!list && !mem::init())
        {
            return 3;
        }

        int passed = 0, failed = 0, skipped = 0, bugs = 0;
        double start = seconds();
        for (Case* c = s_first; c != NULL; c = c->next)
        {
            char name[160];
            snprintf(name, sizeof(name), "%s.%s", c->group, c->name);
            if (only != NULL && strstr(name, only) == NULL)
            {
                continue;
            }
            if (list)
            {
                printf("%s\n", name);
                continue;
            }
            s_failures = 0;
            s_skipped = false;
            s_knownBug = false;
            s_note[0] = '\0';
            s_detailsUsed = 0;
            s_details[0] = '\0';
            log::clear();
            if (prepare(*c))
            {
                fakes::reset();
                u32 code = 0, address = 0;
                if (!guarded(c->run, code, address))
                {
                    fail(__FILE__, __LINE__,
                         "the test crashed: exception %08lx%s%08lx. The mod's code used an address that is neither in "
                         "the memory image nor a host object.",
                         code, code == EXCEPTION_ACCESS_VIOLATION ? " at address " : ", ", address);
                }
            }
            if (s_knownBug && !s_skipped)
            {
                if (s_failures > 0)
                {
                    // The bug is still there: listed with what the test
                    // found, and the run does not fail for it.
                    printf("bug  %s: %s\n%s", name, s_bug, s_details);
                    bugs++;
                    fflush(stdout);
                    continue;
                }
                fail(__FILE__, __LINE__,
                     "this test passes now: the bug it is marked with is gone (%s). Take its KNOWN_BUG line out.", s_bug);
            }
            const char* verdict = s_failures > 0 ? "FAIL" : (s_skipped ? "skip" : "ok  ");
            printf("%s %s%s%s\n", verdict, name, s_note[0] != '\0' ? ": " : "", s_note);
            if (s_failures > 0)
            {
                printf("%s", s_details);
                failed++;
            }
            else if (s_skipped)
            {
                skipped++;
            }
            else
            {
                passed++;
            }
            fflush(stdout);
        }
        if (list)
        {
            return 0;
        }
        char known[64] = "";
        if (bugs > 0)
        {
            snprintf(known, sizeof(known), ", %d known bug%s shown", bugs, bugs == 1 ? "" : "s");
        }
        printf("host tests: %d passed, %d failed, %d skipped%s in %.2f s%s\n", passed, failed, skipped, known,
               seconds() - start, haveDump() ? "" : " (no memory dump given: the tests that need one were skipped)");
        return failed > 0 ? 1 : 0;
    }
}
}

int main(int argc, char** argv)
{
    return host::test::runAll(argc, argv);
}
