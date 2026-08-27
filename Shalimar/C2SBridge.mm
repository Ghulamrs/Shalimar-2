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

C2SResult c2s_c_to_shalimar(const char *source, const char *name) {
    C2SResult out;
    out.ok = 0; out.beyondCount = 0; out.output = nullptr; out.report = nullptr;

    const c2s::Converter::Result r =
        c2s::Converter::convert(source ? source : "", name ? name : "input.c",
                                c2s::Direction::CToShalimar);

    std::ostringstream report;
    for (std::size_t i = 0; i < r.diagnostics.size(); ++i) {
        const c2s::Diagnostic &d = r.diagnostics[i];
        report << d.where().line() << ':' << d.where().column() << ": "
               << d.code() << ": " << d.message() << '\n';
    }

    out.ok = r.ok ? 1 : 0;
    out.beyondCount = r.beyondCount;
    out.output = dup(r.output);
    out.report = dup(report.str());
    return out;
}

void c2s_free(C2SResult *r) {
    if (!r) return;
    std::free(r->output); std::free(r->report);
    r->output = nullptr; r->report = nullptr;
}
