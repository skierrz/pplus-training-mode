#pragma once

// What the mod logged (TM_LOG, through the stand-in for the stub's log
// service in host/fakes.cpp) since the test began.
namespace host {
namespace log {
    void clear();
    void write(const char* format, va_list args);
    const char* text();
    // Lines holding `part`.
    int count(const char* part);
    inline bool contains(const char* part)
    {
        return count(part) > 0;
    }
    // --log on the command line: every line is also printed as it is logged.
    extern bool g_echo;
}
}
