#ifndef C2S_C_CLEXER_H
#define C2S_C_CLEXER_H

#include <string>

#include "CToken.h"

namespace c2s {

class CLexer {
public:
    explicit CLexer(const std::string &text) : text_(text) {}

    CLexResult tokenize();

private:
    bool step(CLexResult &result);
    bool number(CLexResult &result);
    bool charLiteral(CLexResult &result);
    bool stringLiteral(CLexResult &result);
    bool punct(CLexResult &result);
    bool escape(long long *value, std::string *spelling, CLexResult &result);
    bool fail(CLexResult &result, const std::string &message);

    char at(std::size_t i) const { return i < text_.size() ? text_[i] : '\0'; }

    const std::string &text_;
    std::size_t i_ = 0;
    std::size_t tokenStart_ = 0;
};

}

#endif
