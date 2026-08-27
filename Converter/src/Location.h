#ifndef C2S_LOCATION_H
#define C2S_LOCATION_H

#include <string>

namespace c2s {

class Location {
public:
    Location() : line_(0), column_(0) {}
    Location(std::string file, int line, int column)
        : file_(std::move(file)), line_(line), column_(column) {}

    const std::string &file() const { return file_; }
    int line() const { return line_; }
    int column() const { return column_; }

    bool isKnown() const { return line_ > 0; }

    std::string spelling() const;

private:
    std::string file_;
    int line_;
    int column_;
};

}

#endif
