#ifndef C2S_C_CTOKEN_H
#define C2S_C_CTOKEN_H

#include <cstddef>
#include <string>
#include <vector>

namespace c2s {

enum class CTokenKind {
    End,
    Identifier,
    Keyword,
    IntLiteral,
    FloatLiteral,
    CharLiteral,
    StringLiteral,
    Punct
};

struct CToken {
    CTokenKind kind = CTokenKind::End;
    std::string text;
    std::string spelling;
    std::size_t offset = 0;

    long long intValue = 0;
    double floatValue = 0.0;
    bool isUnsigned = false;
    bool isLong = false;
    bool isFloatSuffix = false;
    bool isHexOrOctal = false;

    bool is(const char *s) const { return text == s; }
    bool isKeyword(const char *s) const { return kind == CTokenKind::Keyword && text == s; }
};

struct CLexResult {
    std::vector<CToken> tokens;
    bool failed = false;
    std::size_t errorOffset = 0;
    std::string error;
};

}

#endif
