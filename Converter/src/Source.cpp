#include "Source.h"

#include <fstream>
#include <sstream>

namespace c2s {

std::string Location::spelling() const {
    if (file_.empty() && line_ <= 0) return "<unknown>";
    std::ostringstream out;
    out << (file_.empty() ? "<input>" : file_);
    if (line_ > 0) {
        out << ':' << line_;
        if (column_ > 0) out << ':' << column_;
    }
    return out.str();
}

Source::Source(std::string name, std::string text)
    : name_(std::move(name)), text_(std::move(text)) {
    indexLines();
}

Source Source::fromFile(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::in | std::ios::binary);
    if (!in) throw SourceError("cannot read '" + path + "'");

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) throw SourceError("cannot read '" + path + "'");

    std::string text = buffer.str();

    if (!text.empty() && text[text.size() - 1] != '\n') text += '\n';

    return Source(path, text);
}

void Source::indexLines() {
    lineStarts_.clear();
    lineStarts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n' && i + 1 < text_.size()) lineStarts_.push_back(i + 1);
    }
}

std::size_t Source::offsetOfLine(int lineNo) const {
    if (lineNo < 1) return 0;
    if (static_cast<std::size_t>(lineNo) > lineStarts_.size()) return text_.size();
    return lineStarts_[static_cast<std::size_t>(lineNo - 1)];
}

std::string Source::line(int lineNo) const {
    if (lineNo < 1 || static_cast<std::size_t>(lineNo) > lineStarts_.size()) return std::string();

    const std::size_t begin = lineStarts_[static_cast<std::size_t>(lineNo - 1)];
    std::size_t end = begin;
    while (end < text_.size() && text_[end] != '\n') ++end;

    if (end > begin && text_[end - 1] == '\r') --end;

    return text_.substr(begin, end - begin);
}

Location Source::locate(std::size_t offset) const {
    if (offset > text_.size()) offset = text_.size();

    std::size_t low = 0;
    std::size_t high = lineStarts_.size() - 1;
    while (low < high) {
        const std::size_t mid = low + (high - low + 1) / 2;
        if (lineStarts_[mid] <= offset) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }

    const int lineNo = static_cast<int>(low) + 1;
    const int column = static_cast<int>(offset - lineStarts_[low]) + 1;
    return Location(name_, lineNo, column);
}

}
