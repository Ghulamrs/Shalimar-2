#include "CLexer.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace c2s {

namespace {

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}
bool isIdentBody(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

const char *const kKeywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do",
    "double", "else", "enum", "extern", "float", "for", "goto", "if",
    "int", "long", "register", "return", "short", "signed", "sizeof",
    "static", "struct", "switch", "typedef", "union", "unsigned", "void",
    "volatile", "while"
};

bool isKeyword(const std::string &word) {
    for (std::size_t i = 0; i < sizeof kKeywords / sizeof kKeywords[0]; ++i) {
        if (word == kKeywords[i]) return true;
    }
    return false;
}

const char *const kPuncts3[] = {"<<=", ">>=", "..."};
const char *const kPuncts2[] = {
    "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
    "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|="
};
const char kPuncts1[] = "[](){}.&*+-~!/%<>^|?:;=,";

}

CLexResult CLexer::tokenize() {
    CLexResult result;
    while (i_ < text_.size()) {
        tokenStart_ = i_;
        if (!step(result)) return result;
    }
    return result;
}

bool CLexer::fail(CLexResult &result, const std::string &message) {
    result.failed = true;
    result.errorOffset = tokenStart_;
    result.error = message;
    return false;
}

bool CLexer::step(CLexResult &result) {
    const char c = at(i_);

    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
        c == '\v' || c == '\f') {
        ++i_;
        return true;
    }

    if (c == '/' && at(i_ + 1) == '*') {
        i_ += 2;
        while (i_ < text_.size() && !(at(i_) == '*' && at(i_ + 1) == '/')) ++i_;
        if (i_ >= text_.size()) return fail(result, "unterminated comment");
        i_ += 2;
        return true;
    }
    if (c == '/' && at(i_ + 1) == '/') {
        while (i_ < text_.size() && at(i_) != '\n') ++i_;
        return true;
    }

    if (isIdentStart(c)) {
        std::size_t begin = i_;
        while (isIdentBody(at(i_))) ++i_;
        CToken token;
        token.text = text_.substr(begin, i_ - begin);
        token.spelling = token.text;
        token.offset = begin;
        token.kind = isKeyword(token.text) ? CTokenKind::Keyword : CTokenKind::Identifier;
        result.tokens.push_back(token);
        return true;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
        (c == '.' && std::isdigit(static_cast<unsigned char>(at(i_ + 1))) != 0)) {
        return number(result);
    }

    if (c == '\'') return charLiteral(result);
    if (c == '"') return stringLiteral(result);
    if (c == 'L' && (at(i_ + 1) == '\'' || at(i_ + 1) == '"')) {

        return fail(result, "wide literals are not accepted");
    }

    return punct(result);
}

bool CLexer::number(CLexResult &result) {
    const std::size_t begin = i_;

    bool isFloat = false;
    bool hex = false;

    if (at(i_) == '0' && (at(i_ + 1) == 'x' || at(i_ + 1) == 'X')) {
        hex = true;
        i_ += 2;
        while (std::isxdigit(static_cast<unsigned char>(at(i_))) != 0) ++i_;
    } else {
        while (std::isdigit(static_cast<unsigned char>(at(i_))) != 0) ++i_;
        if (at(i_) == '.') {
            isFloat = true;
            ++i_;
            while (std::isdigit(static_cast<unsigned char>(at(i_))) != 0) ++i_;
        }
        if (at(i_) == 'e' || at(i_) == 'E') {
            std::size_t k = i_ + 1;
            if (at(k) == '+' || at(k) == '-') ++k;
            if (std::isdigit(static_cast<unsigned char>(at(k))) != 0) {
                isFloat = true;
                i_ = k;
                while (std::isdigit(static_cast<unsigned char>(at(i_))) != 0) ++i_;
            }
        }
    }

    CToken token;
    token.offset = begin;

    if (isFloat) {
        if (at(i_) == 'f' || at(i_) == 'F') { token.isFloatSuffix = true; ++i_; }
        else if (at(i_) == 'l' || at(i_) == 'L') { token.isLong = true; ++i_; }
    } else {
        for (;;) {
            if ((at(i_) == 'u' || at(i_) == 'U') && !token.isUnsigned) {
                token.isUnsigned = true; ++i_;
            } else if ((at(i_) == 'l' || at(i_) == 'L') && !token.isLong) {
                token.isLong = true; ++i_;
            } else {
                break;
            }
        }
    }

    token.spelling = text_.substr(begin, i_ - begin);
    token.text = token.spelling;

    const std::string digits = text_.substr(begin, i_ - begin);
    errno = 0;
    if (isFloat) {
        token.kind = CTokenKind::FloatLiteral;
        token.floatValue = std::strtod(digits.c_str(), nullptr);
    } else {
        token.kind = CTokenKind::IntLiteral;

        token.intValue = std::strtoll(digits.c_str(), nullptr, 0);
        token.isHexOrOctal =
            hex || (digits.size() > 1 && digits[0] == '0' &&
                    std::isdigit(static_cast<unsigned char>(digits[1])) != 0);
    }
    if (errno == ERANGE) return fail(result, "number out of range: " + digits);

    result.tokens.push_back(token);
    return true;
}

bool CLexer::escape(long long *value, std::string *spelling, CLexResult &result) {

    *spelling += '\\';
    ++i_;
    const char c = at(i_);
    switch (c) {
        case 'n': *value = '\n'; break;
        case 't': *value = '\t'; break;
        case 'v': *value = '\v'; break;
        case 'b': *value = '\b'; break;
        case 'r': *value = '\r'; break;
        case 'f': *value = '\f'; break;
        case 'a': *value = '\a'; break;
        case '\\': *value = '\\'; break;
        case '\'': *value = '\''; break;
        case '"': *value = '"'; break;
        case '?': *value = '?'; break;
        case 'x': {
            *spelling += c;
            ++i_;
            long long v = 0;
            bool any = false;
            while (std::isxdigit(static_cast<unsigned char>(at(i_))) != 0) {
                const char h = at(i_);
                const int digit = std::isdigit(static_cast<unsigned char>(h)) != 0
                                      ? h - '0'
                                      : (std::tolower(h) - 'a') + 10;
                v = v * 16 + digit;
                *spelling += h;
                ++i_;
                any = true;
            }
            if (!any) return fail(result, "\\x needs hex digits");
            *value = v;
            return true;
        }
        default: {
            if (c >= '0' && c <= '7') {
                long long v = 0;
                int count = 0;
                while (count < 3 && at(i_) >= '0' && at(i_) <= '7') {
                    v = v * 8 + (at(i_) - '0');
                    *spelling += at(i_);
                    ++i_;
                    ++count;
                }
                *value = v;
                return true;
            }
            return fail(result, std::string("unknown escape '\\") + c + "'");
        }
    }
    *spelling += c;
    ++i_;
    return true;
}

bool CLexer::charLiteral(CLexResult &result) {
    CToken token;
    token.kind = CTokenKind::CharLiteral;
    token.offset = i_;
    std::string spelling = "'";
    ++i_;

    if (at(i_) == '\'' || at(i_) == '\0')
        return fail(result, "empty character constant");

    long long value = 0;
    if (at(i_) == '\\') {
        if (!escape(&value, &spelling, result)) return false;
    } else {
        value = static_cast<unsigned char>(at(i_));
        spelling += at(i_);
        ++i_;
    }

    if (at(i_) != '\'') return fail(result, "unterminated character constant");
    spelling += '\'';
    ++i_;

    token.intValue = value;
    token.spelling = spelling;
    token.text = spelling;
    result.tokens.push_back(token);
    return true;
}

bool CLexer::stringLiteral(CLexResult &result) {
    CToken token;
    token.kind = CTokenKind::StringLiteral;
    token.offset = i_;
    std::string spelling = "\"";
    std::string decoded;
    ++i_;

    while (at(i_) != '"') {
        if (at(i_) == '\0' || at(i_) == '\n')
            return fail(result, "unterminated string literal");
        if (at(i_) == '\\') {
            long long value = 0;
            if (!escape(&value, &spelling, result)) return false;
            decoded += static_cast<char>(value);
        } else {
            decoded += at(i_);
            spelling += at(i_);
            ++i_;
        }
    }
    spelling += '"';
    ++i_;

    token.text = decoded;
    token.spelling = spelling;
    result.tokens.push_back(token);
    return true;
}

bool CLexer::punct(CLexResult &result) {
    CToken token;
    token.kind = CTokenKind::Punct;
    token.offset = i_;

    for (std::size_t k = 0; k < sizeof kPuncts3 / sizeof kPuncts3[0]; ++k) {
        const char *p = kPuncts3[k];
        if (at(i_) == p[0] && at(i_ + 1) == p[1] && at(i_ + 2) == p[2]) {
            token.text = p;
            token.spelling = p;
            i_ += 3;
            result.tokens.push_back(token);
            return true;
        }
    }
    for (std::size_t k = 0; k < sizeof kPuncts2 / sizeof kPuncts2[0]; ++k) {
        const char *p = kPuncts2[k];
        if (at(i_) == p[0] && at(i_ + 1) == p[1]) {
            token.text = p;
            token.spelling = p;
            i_ += 2;
            result.tokens.push_back(token);
            return true;
        }
    }
    const char c = at(i_);
    for (std::size_t k = 0; kPuncts1[k] != '\0'; ++k) {
        if (c == kPuncts1[k]) {
            token.text = std::string(1, c);
            token.spelling = token.text;
            ++i_;
            result.tokens.push_back(token);
            return true;
        }
    }

    return fail(result, std::string("stray '") + c + "' in program");
}

}
