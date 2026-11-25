#pragma once

// The texts the mod draws that a test cannot reach through a table of the
// compiled sources: tests/host/build.py reads them out of the sources
// (tests/host/text_scan.py finds every string literal, tests/host/
// text_rules.py says what each one is) and writes this table into the
// build folder. tests/host/tests/test_text_fit.cpp checks every item.
namespace host {
namespace text {
    struct Item {
        // The kind of place the text is drawn in (text_rules.py lists
        // them); "problem" for something text_rules.py could not place.
        const char* kind;
        const char* where; // source and line
        // The text as drawn, a printf-style one with its longest values.
        const char* text;
        float n[4];        // numbers of the place, by kind
    };

    // Ends with an item whose kind is NULL.
    extern const Item kItems[];
}
}
