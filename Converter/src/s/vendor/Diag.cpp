#include "Diag.h"

namespace shalimar {

std::string Message::formatted(const std::vector<std::string>& units) const {
    std::string out = severity == Severity::Error ? "Error: " : "Warning: ";
    if (line > 0) {

        if (unit > 0 && static_cast<size_t>(unit) < units.size()) {
            out += units[static_cast<size_t>(unit)];
            out += " ";
        }
        out += "line ";
        out += std::to_string(line);
        out += ": ";
    }
    out += text;
    return out;
}

void Diagnostics::error(int line, const std::string& text) {
    error(0, line, text);
}

void Diagnostics::warning(int line, const std::string& text) {
    warning(0, line, text);
}

void Diagnostics::error(int unit, int line, const std::string& text) {
    messages_.push_back(Message{Severity::Error, line, text, unit});
    ++errors_;
}

void Diagnostics::warning(int unit, int line, const std::string& text) {
    messages_.push_back(Message{Severity::Warning, line, text, unit});
}

void Diagnostics::unsupported(int line, const std::string& text) {
    unsupported_.push_back(Message{Severity::Error, line, text});
}

void Diagnostics::writeTo(std::string& out) const {
    for (const Message& m : messages_) {
        out += m.formatted(units_);
        out += '\n';
    }
}

}
