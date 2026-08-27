
#include "Token.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace shalimar {
namespace {

bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

bool isAsciiLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool startsIdentifier(char c) { return isAsciiLetter(c) || c == '_'; }
bool continuesIdentifier(char c) { return isAsciiLetter(c) || isAsciiDigit(c) || c == '_'; }

char lowered(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

std::string lowercased(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(lowered(c));
    return out;
}

struct Scalar {
    unsigned value;
    size_t length;
};

Scalar decodeUtf8(const std::string& s, size_t i) {
    unsigned char b0 = static_cast<unsigned char>(s[i]);
    size_t extra = 0;
    unsigned value = 0;
    if (b0 < 0x80) {
        return Scalar{b0, 1};
    } else if ((b0 & 0xE0) == 0xC0) {
        extra = 1; value = b0 & 0x1Fu;
    } else if ((b0 & 0xF0) == 0xE0) {
        extra = 2; value = b0 & 0x0Fu;
    } else if ((b0 & 0xF8) == 0xF0) {
        extra = 3; value = b0 & 0x07u;
    } else {
        return Scalar{b0, 1};
    }
    if (i + extra >= s.size()) return Scalar{b0, 1};
    for (size_t k = 1; k <= extra; ++k) {
        unsigned char b = static_cast<unsigned char>(s[i + k]);
        if ((b & 0xC0) != 0x80) return Scalar{b0, 1};
        value = (value << 6) | (b & 0x3Fu);
    }
    return Scalar{value, extra + 1};
}

std::string codePointName(unsigned value) {
    static const char* hex = "0123456789ABCDEF";
    std::string digits;
    unsigned v = value;
    do {
        digits.insert(digits.begin(), hex[v & 0xFu]);
        v >>= 4;
    } while (v != 0);
    while (digits.size() < 4) digits.insert(digits.begin(), '0');
    return "U+" + digits;
}

class Lexer {
public:
    explicit Lexer(const std::string& source) : src_(source) {}

    LexResult run() {
        while (i_ < src_.size()) {
            tokenLine_ = line_;
            if (!step()) return result_;
        }
        return result_;
    }

private:
    const std::string& src_;
    size_t i_ = 0;
    int line_ = 1;
    int tokenLine_ = 1;
    LexResult result_;

    char at(size_t k) const { return k < src_.size() ? src_[k] : '\0'; }
    char cur() const { return at(i_); }
    char next() const { return at(i_ + 1); }

    bool fail(const std::string& text) {
        result_.failed = true;
        result_.errorLine = tokenLine_;
        result_.error = text;
        return false;
    }

    void push(Tok kind) {
        Token t;
        t.kind = kind;
        t.line = tokenLine_;
        result_.tokens.push_back(t);
    }

    void pushOperator(const char* spelling) {
        Token t;
        t.kind = Tok::Operator;
        t.line = tokenLine_;
        t.text = spelling;
        result_.tokens.push_back(t);
    }

    bool step() {
        char c = cur();

        if (c == '/' && next() == '/') {
            while (i_ < src_.size() && src_[i_] != '\n') ++i_;
            return true;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (c == '\n') ++line_;
            ++i_;
            return true;
        }
        if (c == '"') return string();
        if (startsIdentifier(c)) return word();
        if (isAsciiDigit(c)) return number();
        return punctuation();
    }

    bool string() {
        size_t j = i_ + 1;
        while (j < src_.size() && src_[j] != '"' && src_[j] != '\n') ++j;
        if (j >= src_.size() || src_[j] != '"') return fail("Unclosed string - add '\"'");
        Token t;
        t.kind = Tok::StringLiteral;
        t.line = tokenLine_;
        t.text = src_.substr(i_ + 1, j - i_ - 1);
        result_.tokens.push_back(t);
        i_ = j + 1;
        return true;
    }

    bool word() {
        size_t j = i_;
        while (j < src_.size() && continuesIdentifier(src_[j])) ++j;
        std::string raw = src_.substr(i_, j - i_);
        i_ = j;

        const std::string key = lowercased(raw);
        if (key == "if")       { push(Tok::If);       return true; }
        if (key == "else")     { push(Tok::Else);     return true; }
        if (key == "while")    { push(Tok::While);    return true; }
        if (key == "for")      { push(Tok::For);      return true; }
        if (key == "to")       { push(Tok::To);       return true; }
        if (key == "step")     { push(Tok::Step);     return true; }
        if (key == "fun")      { push(Tok::Fun);      return true; }
        if (key == "uses")     { push(Tok::Uses);     return true; }
        if (key == "return")   { push(Tok::Return);   return true; }
        if (key == "break")    { push(Tok::Break);    return true; }
        if (key == "continue") { push(Tok::Continue); return true; }
        if (key == "int")      { push(Tok::Int);      return true; }
        if (key == "real")     { push(Tok::Real);     return true; }
        if (key == "char")     { push(Tok::Char);     return true; }

        Token t;
        t.kind = Tok::Identifier;
        t.line = tokenLine_;
        t.text = raw;
        result_.tokens.push_back(t);
        return true;
    }

    bool number() {
        size_t j = i_;
        while (j < src_.size() && (isAsciiDigit(src_[j]) || src_[j] == '.')) ++j;
        bool exponent = false;
        if (j < src_.size() && (src_[j] == 'e' || src_[j] == 'E')) {
            size_t k = j + 1;
            if (k < src_.size() && (src_[k] == '+' || src_[k] == '-')) ++k;
            while (k < src_.size() && isAsciiDigit(src_[k])) ++k;
            j = k;
            exponent = true;
        }
        std::string raw = src_.substr(i_, j - i_);
        i_ = j;

        bool hasDot = raw.find('.') != std::string::npos;
        if (hasDot || exponent) {
            const char* begin = raw.c_str();
            char* end = nullptr;
            errno = 0;
            double value = std::strtod(begin, &end);
            if (end != begin + raw.size() || end == begin || !std::isfinite(value)) {
                return fail("Malformed number '" + raw + "'");
            }
            Token t;
            t.kind = Tok::RealLiteral;
            t.line = tokenLine_;
            t.realValue = value;
            result_.tokens.push_back(t);
            return true;
        }

        const char* begin = raw.c_str();
        char* end = nullptr;
        errno = 0;
        long long wide = std::strtoll(begin, &end, 10);
        if (end != begin + raw.size() || errno == ERANGE ||
            wide > 2147483647LL || wide < -2147483648LL) {
            return fail("'" + raw + "' is too big for int - add '.'");
        }
        Token t;
        t.kind = Tok::IntLiteral;
        t.line = tokenLine_;
        t.intValue = static_cast<int32_t>(wide);
        result_.tokens.push_back(t);
        return true;
    }

    bool punctuation() {
        char c = cur();
        char d = next();

        if (c == '+' && d == ':') { i_ += 2; push(Tok::PlusAssign);  return true; }
        if (c == '-' && d == ':') { i_ += 2; push(Tok::MinusAssign); return true; }
        if (c == '!' && d == '=') { i_ += 2; pushOperator("!=");     return true; }
        if (c == '<' && d == '=') { i_ += 2; pushOperator("<=");     return true; }
        if (c == '>' && d == '=') { i_ += 2; pushOperator(">=");     return true; }
        if (c == '?' && d == '?') { i_ += 2; push(Tok::PrintInline); return true; }
        if (c == '?')             { i_ += 1; push(Tok::PrintLine);   return true; }

        if (c == '!') return fail("'!' is not a command - use '?\?' or '!='");

        switch (c) {
        case '(': ++i_; push(Tok::ParensOpen);   return true;
        case ')': ++i_; push(Tok::ParensClose);  return true;
        case '{': ++i_; push(Tok::BraceOpen);    return true;
        case '}': ++i_; push(Tok::BraceClose);   return true;
        case '[': ++i_; push(Tok::BracketOpen);  return true;
        case ']': ++i_; push(Tok::BracketClose); return true;
        case ',': ++i_; push(Tok::Comma);        return true;
        case '.': ++i_; push(Tok::Dot);          return true;
        case ':': ++i_; push(Tok::Assign);       return true;
        default: break;
        }

        if (c == '-' || c == '+' || c == '*' || c == '/' || c == '%' || c == '^' ||
            c == '=' || c == '<' || c == '>' || c == '&' || c == '|') {
            const char spelling[2] = {c, '\0'};
            ++i_;
            pushOperator(spelling);
            return true;
        }

        Scalar s = decodeUtf8(src_, i_);
        std::string glyph = src_.substr(i_, s.length);
        return fail("Unexpected character '" + glyph + "' (" + codePointName(s.value) + ")");
    }
};

}

LexResult tokenize(const std::string& source) {
    return Lexer(source).run();
}

}

namespace shalimar {

std::string spellingOf(const Token& token) {
    switch (token.kind) {
    case Tok::IntLiteral:    return std::to_string(token.intValue);
    case Tok::RealLiteral:   return "number";
    case Tok::StringLiteral: return "\"" + token.text + "\"";
    case Tok::Identifier:    return token.text;
    case Tok::Operator:      return token.text;
    case Tok::Assign:        return ":";
    case Tok::PlusAssign:    return "+:";
    case Tok::MinusAssign:   return "-:";
    case Tok::PrintLine:     return "?";
    case Tok::PrintInline:   return "?\?";
    case Tok::ParensOpen:    return "(";
    case Tok::ParensClose:   return ")";
    case Tok::BraceOpen:     return "{";
    case Tok::BraceClose:    return "}";
    case Tok::BracketOpen:   return "[";
    case Tok::BracketClose:  return "]";
    case Tok::Comma:         return ",";
    case Tok::Dot:           return ".";
    case Tok::If:            return "if";
    case Tok::Else:          return "else";
    case Tok::While:         return "while";
    case Tok::For:           return "for";
    case Tok::To:            return "to";
    case Tok::Step:          return "step";
    case Tok::Fun:           return "fun";
    case Tok::Uses:          return "uses";
    case Tok::Return:        return "return";
    case Tok::Break:         return "break";
    case Tok::Continue:      return "continue";
    case Tok::Int:           return "int";
    case Tok::Real:          return "real";
    case Tok::Char:          return "char";
    case Tok::EndOfInput:    return "";
    }
    return "";
}

}
