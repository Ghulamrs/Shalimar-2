
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace shalimar {

enum class Tok {
    IntLiteral,
    RealLiteral,
    StringLiteral,
    Identifier,

    Operator,

    Assign,
    PlusAssign,
    MinusAssign,
    PrintLine,
    PrintInline,
    ParensOpen, ParensClose,
    BraceOpen, BraceClose,
    BracketOpen, BracketClose,
    Comma, Dot,

    If, Else, While, For, To, Step, Fun, Return, Uses,
    Break, Continue,
    Int, Real, Char,

    EndOfInput
};

struct Token {
    Tok kind = Tok::EndOfInput;
    int line = 0;

    std::string text;
    int32_t intValue = 0;
    double realValue = 0.0;
};

struct LexResult {
    std::vector<Token> tokens;
    bool failed = false;
    int errorLine = 0;
    std::string error;
};

LexResult tokenize(const std::string& source);

std::string spellingOf(const Token& token);

}
