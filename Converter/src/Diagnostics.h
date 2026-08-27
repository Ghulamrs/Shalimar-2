#ifndef C2S_DIAGNOSTICS_H
#define C2S_DIAGNOSTICS_H

#include <iosfwd>
#include <string>
#include <vector>

#include "Location.h"

namespace c2s {

class Source;

enum class Severity {
    Note,
    Warning,
    SyntaxError,
    ConversionError,
    PreprocessorFix
};

const char *spellingOf(Severity severity);

class Diagnostic {
public:
    Diagnostic(Severity severity, Location where, std::string code, std::string message)
        : severity_(severity),
          where_(std::move(where)),
          code_(std::move(code)),
          message_(std::move(message)) {}

    Severity severity() const { return severity_; }
    const Location &where() const { return where_; }
    const std::string &code() const { return code_; }
    const std::string &message() const { return message_; }
    const std::string &snippet() const { return snippet_; }
    const std::string &hint() const { return hint_; }

    void setSnippet(std::string text) { snippet_ = std::move(text); }
    void setHint(std::string text) { hint_ = std::move(text); }

    bool isError() const {
        return severity_ == Severity::SyntaxError ||
               severity_ == Severity::ConversionError ||
               severity_ == Severity::PreprocessorFix;
    }

    std::string formatted() const;

private:
    Severity severity_;
    Location where_;
    std::string code_;
    std::string message_;
    std::string snippet_;
    std::string hint_;
};

class Diagnostics {
public:
    Diagnostics() : errors_(0), warnings_(0) {}

    void add(Diagnostic diagnostic);

    void report(Severity severity, const Source &source, const Location &where,
                const std::string &code, const std::string &message,
                const std::string &hint = std::string());

    void report(Severity severity, const std::string &code, const std::string &message);

    bool hasErrors() const { return errors_ > 0; }
    int errorCount() const { return errors_; }
    int warningCount() const { return warnings_; }
    bool empty() const { return messages_.empty(); }

    bool hasPreprocessorFixes() const;

    const std::vector<Diagnostic> &messages() const { return messages_; }

    void writeTo(std::ostream &out) const;

    std::string summary() const;

private:
    std::vector<Diagnostic> messages_;
    int errors_;
    int warnings_;
};

}

#endif
