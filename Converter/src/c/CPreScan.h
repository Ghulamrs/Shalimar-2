#ifndef C2S_C_CPRESCAN_H
#define C2S_C_CPRESCAN_H

#include <string>
#include <vector>

namespace c2s {

class Source;
class Diagnostics;

class CPreScan {
public:
    struct Include {
        std::string header;
        bool angled = false;
        int line = 0;
    };

    struct Macro {
        std::string name;
        bool functionLike = false;
        std::vector<std::string> params;
        std::string body;
        int line = 0;
        std::size_t offset = 0;
    };

    // An `#ifndef NAME` whose block holds nothing but the `#define NAME` it
    // guards, closed by a plain `#endif`. Dropped rather than asked about -
    // see CPreScan.cpp for why that one shape is safe when no conditional is.
    struct Guard {
        std::string name;
        int openLine = 0;
        int defineLine = 0;
        int closeLine = 0;
    };

    bool run(const Source &source, Diagnostics &diagnostics);

    const std::string &text() const { return text_; }

    const std::vector<Include> &includes() const { return includes_; }

    const std::vector<Macro> &macros() const { return macros_; }

    const std::vector<std::string> &pending() const { return pending_; }

    const std::vector<Guard> &guards() const { return guards_; }

private:
    std::string text_;
    std::vector<Include> includes_;
    std::vector<Macro> macros_;
    std::vector<std::string> pending_;
    std::vector<Guard> guards_;
};

}

#endif
