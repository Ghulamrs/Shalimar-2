#include "CPreScan.h"

#include <cctype>
#include <set>

#include "../Diagnostics.h"
#include "../Source.h"

namespace c2s {

namespace {

std::string directiveName(const std::string &line, std::size_t hash) {
    std::size_t i = hash + 1;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    std::size_t begin = i;
    while (i < line.size() && std::isalpha(static_cast<unsigned char>(line[i])) != 0) ++i;
    return line.substr(begin, i - begin);
}

std::size_t afterDirective(const std::string &line, std::size_t hash) {
    std::size_t i = hash + 1;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    while (i < line.size() && std::isalpha(static_cast<unsigned char>(line[i])) != 0) ++i;
    return i;
}

std::string adviceFor(const std::string &name) {
    if (name == "define")
        return "expand or inline the macro by hand, or make it a variable or a function";

    if (name == "undef")
        return "remove it along with the #define it cancels";
    if (name == "if" || name == "ifdef" || name == "ifndef" ||
        name == "elif" || name == "else" || name == "endif")
        return "decide the condition by hand and keep only the branch that is wanted";
    if (name == "pragma")
        return "remove it; a pragma has no meaning to the target language";
    if (name == "error")
        return "resolve whatever the #error guards and remove it";
    if (name == "line")
        return "remove it; the line numbers of the file itself are what diagnostics use";
    return "resolve it by hand and remove it";
}

bool isNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isNameStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

void namesIn(const std::string &line, std::size_t from,
             std::set<std::string> &into) {
    std::size_t i = from;
    while (i < line.size()) {
        if (!isNameStart(line[i])) { ++i; continue; }
        const std::size_t begin = i;
        while (i < line.size() && isNameChar(line[i])) ++i;
        into.insert(line.substr(begin, i - begin));
    }
}

bool parseDefine(const std::string &line, std::size_t afterName,
                 CPreScan::Macro *out) {
    std::size_t i = afterName;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || !isNameStart(line[i])) return false;

    const std::size_t nameBegin = i;
    while (i < line.size() && isNameChar(line[i])) ++i;
    out->name = line.substr(nameBegin, i - nameBegin);

    if (i < line.size() && line[i] == '(') {
        out->functionLike = true;
        ++i;
        for (;;) {
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i < line.size() && line[i] == ')') { ++i; break; }
            if (i >= line.size() || !isNameStart(line[i])) return false;
            const std::size_t paramBegin = i;
            while (i < line.size() && isNameChar(line[i])) ++i;
            out->params.push_back(line.substr(paramBegin, i - paramBegin));
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
            if (i < line.size() && line[i] == ',') { ++i; continue; }
            if (i < line.size() && line[i] == ')') { ++i; break; }
            return false;
        }
    }

    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    std::size_t end = line.size();
    while (end > i && (line[end - 1] == ' ' || line[end - 1] == '\t' ||
                       line[end - 1] == '\r')) --end;
    out->body = line.substr(i, end - i);

    if (out->body.find('#') != std::string::npos) return false;

    if (!out->body.empty() && out->body[out->body.size() - 1] == '\\') return false;
    return true;
}

bool parseInclude(const std::string &line, std::size_t afterName,
                  CPreScan::Include *out) {
    std::size_t i = afterName;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size()) return false;

    char open = line[i];
    char close = open == '<' ? '>' : (open == '"' ? '"' : '\0');
    if (close == '\0') return false;

    std::size_t begin = ++i;
    while (i < line.size() && line[i] != close) ++i;
    if (i >= line.size()) return false;

    out->header = line.substr(begin, i - begin);
    out->angled = open == '<';
    return true;
}

// The name a directive names, if it names one: `#ifndef M_PI` or
// `#define M_PI 3.14` both answer M_PI.
std::string nameAfterDirective(const std::string &line, std::size_t hash) {
    std::size_t i = afterDirective(line, hash);
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= line.size() || !isNameStart(line[i])) return std::string();
    const std::size_t begin = i;
    while (i < line.size() && isNameChar(line[i])) ++i;
    return line.substr(begin, i - begin);
}

// Nothing but space, or the start of a comment. Used for what follows the
// name on an `#ifndef`, and for the lines inside the block: a block comment
// spanning several lines counts as content and the guard is then not
// recognised, which is the safe way round to be wrong.
bool blankOrComment(const std::string &line, std::size_t from) {
    std::size_t i = from;
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
    if (i >= line.size()) return true;
    return line[i] == '/' && i + 1 < line.size() && (line[i + 1] == '/' || line[i + 1] == '*');
}

// **The one conditional that decides nothing.**
//
//     #ifndef M_PI
//     #define M_PI 3.14
//     #endif
//
// Every other `#if` asks which program this is, and the answer is not in the
// file - which is why they stop the conversion. This shape does not ask: the
// guard holds nothing but the definition it guards, and a file being converted
// has no other translation unit to have defined the name first, so the
// definition always stood. Dropping the two lines and keeping the middle one
// loses nothing, and refusing it stopped conversions over a header idiom that
// appears in almost every file that wants a constant.
//
// Narrow on purpose. `#ifdef` is not this - it means "only if somebody else
// defined it", which is a real question. An `#else` or `#elif` is a choice
// between programs. A name that does not match the one being defined is not a
// guard around it. Anything else inside the block at all - another directive,
// a declaration, a nested conditional - and it stops being this shape and
// goes back to being a question.
std::vector<CPreScan::Guard> findGuards(const Source &source) {
    struct Open {
        int line = 0;
        std::string name;
        bool candidate = false;
        int defineLine = 0;
        int defines = 0;
        int others = 0;
    };

    std::vector<Open> stack;
    std::vector<CPreScan::Guard> found;
    const int lineTotal = source.lineCount();

    for (int lineNo = 1; lineNo <= lineTotal; ++lineNo) {
        const std::string line = source.line(lineNo);
        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;

        if (i >= line.size() || line[i] != '#') {
            if (!stack.empty() && !blankOrComment(line, i)) ++stack.back().others;
            continue;
        }

        const std::string name = directiveName(line, i);

        if (name == "if" || name == "ifdef" || name == "ifndef") {
            if (!stack.empty()) ++stack.back().others;

            Open open;
            open.line = lineNo;
            if (name == "ifndef") {
                open.name = nameAfterDirective(line, i);
                open.candidate =
                    !open.name.empty() &&
                    blankOrComment(line, line.find(open.name) + open.name.size());
            }
            stack.push_back(open);
            continue;
        }

        if (stack.empty()) continue;

        if (name == "else" || name == "elif") {
            stack.back().candidate = false;
            continue;
        }

        if (name == "endif") {
            const Open open = stack.back();
            stack.pop_back();
            if (open.candidate && open.defines == 1 && open.others == 0) {
                CPreScan::Guard guard;
                guard.name = open.name;
                guard.openLine = open.line;
                guard.defineLine = open.defineLine;
                guard.closeLine = lineNo;
                found.push_back(guard);
            }
            continue;
        }

        if (name == "define" && nameAfterDirective(line, i) == stack.back().name) {
            ++stack.back().defines;
            stack.back().defineLine = lineNo;
            continue;
        }

        ++stack.back().others;
    }

    return found;
}

std::string asWritten(const std::string &line, std::size_t hash) {
    std::size_t end = line.size();
    while (end > hash && (line[end - 1] == ' ' || line[end - 1] == '\t' ||
                          line[end - 1] == '\r')) --end;
    return line.substr(hash, end - hash);
}

}

bool CPreScan::run(const Source &source, Diagnostics &diagnostics) {
    text_ = source.text();
    includes_.clear();
    macros_.clear();
    pending_.clear();
    guards_ = findGuards(source);

    std::set<int> guardOpen;
    std::set<int> guardClose;
    for (std::size_t g = 0; g < guards_.size(); ++g) {
        guardOpen.insert(guards_[g].openLine);
        guardClose.insert(guards_[g].closeLine);
    }

    bool clean = true;
    const int lineTotal = source.lineCount();

    std::set<std::string> decidedBy;
    for (int lineNo = 1; lineNo <= lineTotal; ++lineNo) {
        const std::string line = source.line(lineNo);
        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || line[i] != '#') continue;
        const std::string name = directiveName(line, i);
        // A guard's own `#ifndef` is not a conditional that decides anything,
        // so the name it names is not "decided by" one: the `#define` under
        // it is an ordinary substitution and is taken as one. Without this
        // line the define is still refused - P0103 - and dropping the guard
        // would have changed nothing at all.
        if (guardOpen.count(lineNo) != 0) continue;

        if (name == "if" || name == "ifdef" || name == "ifndef" || name == "elif") {
            namesIn(line, i + 1, decidedBy);
        }
    }

    for (int lineNo = 1; lineNo <= lineTotal; ++lineNo) {
        const std::string line = source.line(lineNo);

        std::size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
        if (i >= line.size() || line[i] != '#') continue;

        const std::string name = directiveName(line, i);
        const Location where(source.name(), lineNo, static_cast<int>(i) + 1);

        if (guardOpen.count(lineNo) != 0 || guardClose.count(lineNo) != 0) {
            // Said once each, after the loop, where what the file includes is
            // known - and that is what decides whether this is a note or a
            // warning.

        } else if (name == "include") {
            Include include;
            include.line = lineNo;
            if (parseInclude(line, afterDirective(line, i), &include)) {
                includes_.push_back(include);
            }

        } else if (name == "define") {
            Macro macro;
            macro.line = lineNo;
            macro.offset = source.offsetOfLine(lineNo) + i;
            if (!parseDefine(line, afterDirective(line, i), &macro)) {
                diagnostics.report(Severity::PreprocessorFix, source, where, "P0102",
                                   "#define in a form this converter cannot expand",
                                   "expand it by hand - a '#' or '##' in the "
                                   "replacement, or a line continuation, has no "
                                   "token-level equivalent here");
                pending_.push_back(asWritten(line, i));
                clean = false;
            } else if (decidedBy.count(macro.name) != 0) {
                diagnostics.report(Severity::PreprocessorFix, source, where, "P0103",
                                   "#define " + macro.name +
                                   " decides which program this is - a "
                                   "conditional tests it",
                                   "settle the condition by hand and keep only "
                                   "the branch that is wanted, then remove both "
                                   "the #define and the #if that reads it");
                pending_.push_back(asWritten(line, i));
                clean = false;
            } else {

                macros_.push_back(macro);
            }
        } else if (name.empty()) {
            diagnostics.report(Severity::PreprocessorFix, source, where, "P0100",
                               "a '#' line the converter does not read",
                               "remove it before converting");
            pending_.push_back(asWritten(line, i));
            clean = false;
        } else {
            diagnostics.report(Severity::PreprocessorFix, source, where, "P0101",
                               "#" + name + " must be resolved before conversion",
                               adviceFor(name));
            pending_.push_back(asWritten(line, i));
            clean = false;
        }

        const std::size_t begin = source.offsetOfLine(lineNo);
        for (std::size_t k = 0; k < line.size(); ++k) {
            text_[begin + k] = ' ';
        }
    }

    // **A dropped guard is safe only if nothing else could have defined the
    // name, and a header could.** `#ifndef M_PI / #define M_PI 3.14 / #endif`
    // beside `#include <math.h>` is the case that proves it: math.h defines
    // M_PI as the full pi, so the C never takes the 3.14 - and a conversion
    // that drops the guard does take it, and computes different numbers. The
    // converter translates nothing from a header and cannot see what one
    // defines, so it says so rather than pretending either way.
    //
    // With no include in the file there is nothing else to have defined it,
    // and the drop is provable rather than likely.
    for (std::size_t g = 0; g < guards_.size(); ++g) {
        const Guard &guard = guards_[g];
        const Location where(source.name(), guard.openLine, 1);

        if (includes_.empty()) {
            diagnostics.report(Severity::Note, source, where, "P0104",
                               "#ifndef " + guard.name +
                                   " guards nothing but its own #define",
                               "dropped, with its #endif: this file includes "
                               "nothing, so nothing else can have defined " +
                                   guard.name);
        } else {
            diagnostics.report(Severity::Warning, source, where, "P0105",
                               "#ifndef " + guard.name +
                                   " was dropped and a header here may define " +
                                   guard.name,
                               "the definition in this file is the one that was "
                               "taken; if a header defines " + guard.name +
                                   " the C took that instead, and the two "
                                   "programs then differ - <math.h> and M_PI "
                                   "are exactly that pair");
        }
    }

    return clean;
}

}
