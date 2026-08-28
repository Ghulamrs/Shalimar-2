// Objective-C++ - the only file that sees both worlds. Note what is NOT here:
// no conversion of any C++ source, no wrapper around a process, no file I/O.
// Converter::convert already takes text and returns text.
#include "C2SBridge.h"
#include "../Converter/src/Converter.h"
#include <cstdlib>
#include <cstring>
#include <sstream>

static char *dup(const std::string &s) {
    char *p = static_cast<char *>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

// Both directions differ by one argument, so they share everything below it.
// The direction is passed explicitly and never inferred: c2s infers it from the
// file's EXTENSION, and this app's files are all named .shm whatever they hold.
static C2SResult convert(const char *source, const char *name,
                         c2s::Direction direction) {
    C2SResult out;
    out.ok = 0; out.beyondCount = 0; out.output = nullptr; out.report = nullptr;
    out.lines = nullptr; out.lineCount = 0;

    const c2s::Converter::Result r =
        c2s::Converter::convert(source ? source : "", name ? name : "input",
                                direction);

    std::ostringstream report;
    for (std::size_t i = 0; i < r.diagnostics.size(); ++i) {
        const c2s::Diagnostic &d = r.diagnostics[i];
        // Tab-separated fields, one diagnostic to a line: line, column,
        // severity, code, message. Deliberately NOT pre-formatted - how a
        // diagnostic is worded in the console is the app's business, and the
        // console has conventions a C++ file should not be deciding for it.
        const bool bad = d.severity() != c2s::Severity::Note &&
                         d.severity() != c2s::Severity::Warning;
        report << d.where().line() << '\t' << d.where().column() << '\t'
               << (bad ? 'E' : 'W') << '\t' << d.code() << '\t'
               << d.message() << '\n';
    }

    out.ok = r.ok ? 1 : 0;
    out.beyondCount = r.beyondCount;
    out.output = dup(r.output);
    out.report = dup(report.str());

    // Copied out of the vector rather than handed over, for the same reason
    // the two strings are: nothing C++ crosses this header, so what Swift is
    // given has to be memory it can hold after the Result is gone.
    if (!r.lineMap.empty()) {
        out.lines = static_cast<int *>(std::malloc(r.lineMap.size() * sizeof(int)));
        if (out.lines != nullptr) {
            for (std::size_t i = 0; i < r.lineMap.size(); ++i) {
                out.lines[i] = r.lineMap[i];
            }
            out.lineCount = static_cast<int>(r.lineMap.size());
        }
    }
    return out;
}

C2SResult c2s_c_to_shalimar(const char *source, const char *name) {
    return convert(source, name, c2s::Direction::CToShalimar);
}

C2SResult c2s_shalimar_to_c(const char *source, const char *name) {
    return convert(source, name, c2s::Direction::ShalimarToC);
}

void c2s_free(C2SResult *r) {
    if (!r) return;
    std::free(r->output); std::free(r->report); std::free(r->lines);
    r->output = nullptr; r->report = nullptr; r->lines = nullptr;
    r->lineCount = 0;
}
