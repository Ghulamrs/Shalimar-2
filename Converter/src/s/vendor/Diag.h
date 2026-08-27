
#pragma once

#include <string>
#include <vector>

namespace shalimar {

enum class Severity { Error, Warning };

struct Message {
    Severity severity;
    int line;
    std::string text;

    int unit = 0;

    std::string formatted(const std::vector<std::string> &units) const;
};

class Diagnostics {
public:
    void error(int line, const std::string& text);
    void warning(int line, const std::string& text);
    void error(int unit, int line, const std::string& text);
    void warning(int unit, int line, const std::string& text);

    void nameUnits(const std::vector<std::string>& names) { units_ = names; }

    void unsupported(int line, const std::string& what);

    bool hasUnsupported() const { return !unsupported_.empty(); }
    const std::vector<Message>& unsupportedItems() const { return unsupported_; }

    bool hasErrors() const { return errors_ > 0; }
    const std::vector<Message>& messages() const { return messages_; }
    void writeTo(std::string& out) const;

private:
    std::vector<Message> messages_;
    std::vector<Message> unsupported_;
    std::vector<std::string> units_;
    int errors_ = 0;
};

}
