#ifndef C2S_S_SBEYOND_H
#define C2S_S_SBEYOND_H

#include <string>
#include <vector>

#include "vendor/Ast.h"

namespace c2s {

class SBeyondStmt : public shalimar::Stmt {
public:
    SBeyondStmt(std::string reason, std::vector<std::string> sourceLines, int line)
        : shalimar::Stmt(line), reason_(std::move(reason)),
          lines_(std::move(sourceLines)) {}

    const std::string &reason() const { return reason_; }
    const std::vector<std::string> &lines() const { return lines_; }

    void accept(shalimar::NodeVisitor &) override {}

private:
    std::string reason_;
    std::vector<std::string> lines_;
};

}

#endif
