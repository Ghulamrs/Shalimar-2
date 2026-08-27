#ifndef C2S_SOURCE_H
#define C2S_SOURCE_H

#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "Location.h"

namespace c2s {

class SourceError : public std::runtime_error {
public:
    explicit SourceError(const std::string &what) : std::runtime_error(what) {}
};

class Source {
public:
    Source(std::string name, std::string text);

    static Source fromFile(const std::string &path);

    const std::string &name() const { return name_; }
    const std::string &text() const { return text_; }
    std::size_t size() const { return text_.size(); }

    int lineCount() const { return static_cast<int>(lineStarts_.size()); }

    std::string line(int lineNo) const;

    Location locate(std::size_t offset) const;

    std::size_t offsetOfLine(int lineNo) const;

private:
    void indexLines();

    std::string name_;
    std::string text_;
    std::vector<std::size_t> lineStarts_;
};

}

#endif
