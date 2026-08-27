#include "Diagnostics.h"

#include <ostream>
#include <sstream>

#include "Source.h"

namespace c2s {

const char *spellingOf(Severity severity) {
    switch (severity) {
        case Severity::Note:            return "note";
        case Severity::Warning:         return "warning";
        case Severity::SyntaxError:     return "syntax error";
        case Severity::ConversionError: return "conversion error";
        case Severity::PreprocessorFix: return "fix before conversion";
    }
    return "message";
}

namespace {

std::string gutter(int lineNo, std::size_t width) {
    std::ostringstream out;
    std::ostringstream number;
    number << lineNo;
    std::string text = number.str();
    while (text.size() < width) text = " " + text;
    out << ' ' << text << " | ";
    return out.str();
}

std::string blankGutter(std::size_t width) {
    std::string out = " ";
    out.append(width, ' ');
    out += " | ";
    return out;
}

std::size_t widthOf(int lineNo) {
    std::ostringstream number;
    number << lineNo;
    return number.str().size();
}

std::string caretRow(const std::string &line, int column) {
    std::string row;
    const int stop = column > 0 ? column - 1 : 0;
    for (int i = 0; i < stop; ++i) {
        if (static_cast<std::size_t>(i) < line.size() && line[static_cast<std::size_t>(i)] == '\t') {
            row += '\t';
        } else {
            row += ' ';
        }
    }
    row += '^';
    return row;
}

}

std::string Diagnostic::formatted() const {
    std::ostringstream out;
    out << where_.spelling() << ": " << spellingOf(severity_);
    if (!code_.empty()) out << " [" << code_ << "]";
    out << ": " << message_;

    if (!snippet_.empty() && where_.line() > 0) {
        const std::size_t width = widthOf(where_.line());
        out << '\n' << gutter(where_.line(), width) << snippet_;
        if (where_.column() > 0) {
            out << '\n' << blankGutter(width) << caretRow(snippet_, where_.column());
        }
    }

    if (!hint_.empty()) out << "\n  = " << hint_;

    return out.str();
}

void Diagnostics::add(Diagnostic diagnostic) {
    if (diagnostic.isError()) {
        ++errors_;
    } else if (diagnostic.severity() == Severity::Warning) {
        ++warnings_;
    }
    messages_.push_back(std::move(diagnostic));
}

void Diagnostics::report(Severity severity, const Source &source, const Location &where,
                         const std::string &code, const std::string &message,
                         const std::string &hint) {
    Location placed = where;
    if (placed.file().empty()) {
        placed = Location(source.name(), where.line(), where.column());
    }

    Diagnostic diagnostic(severity, placed, code, message);
    if (placed.line() > 0) diagnostic.setSnippet(source.line(placed.line()));
    if (!hint.empty()) diagnostic.setHint(hint);
    add(std::move(diagnostic));
}

void Diagnostics::report(Severity severity, const std::string &code,
                         const std::string &message) {
    add(Diagnostic(severity, Location(), code, message));
}

bool Diagnostics::hasPreprocessorFixes() const {
    for (std::size_t i = 0; i < messages_.size(); ++i) {
        if (messages_[i].severity() == Severity::PreprocessorFix) return true;
    }
    return false;
}

void Diagnostics::writeTo(std::ostream &out) const {
    for (std::size_t i = 0; i < messages_.size(); ++i) {
        out << messages_[i].formatted() << '\n';
    }
}

namespace {

std::string plural(int n, const char *one, const char *many) {
    std::ostringstream out;
    out << n << ' ' << (n == 1 ? one : many);
    return out.str();
}

}

std::string Diagnostics::summary() const {
    if (messages_.empty()) return "no diagnostics";

    int syntax = 0;
    int conversion = 0;
    int fixes = 0;
    for (std::size_t i = 0; i < messages_.size(); ++i) {
        switch (messages_[i].severity()) {
            case Severity::SyntaxError:     ++syntax; break;
            case Severity::ConversionError: ++conversion; break;
            case Severity::PreprocessorFix: ++fixes; break;
            default: break;
        }
    }

    std::vector<std::string> parts;
    if (fixes > 0) parts.push_back(plural(fixes, "preprocessor construct to fix",
                                          "preprocessor constructs to fix"));
    if (syntax > 0) parts.push_back(plural(syntax, "syntax error", "syntax errors"));
    if (conversion > 0) parts.push_back(plural(conversion, "conversion error",
                                               "conversion errors"));
    if (warnings_ > 0) parts.push_back(plural(warnings_, "warning", "warnings"));

    // Nothing to count. Notes are not summarised - a run whose only
    // diagnostic is "a guard was dropped" would otherwise end with the words
    // "no diagnostics" underneath the note it just wrote.
    if (parts.empty()) return std::string();

    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += (i + 1 == parts.size() ? " and " : ", ");
        out += parts[i];
    }
    return out;
}

}
