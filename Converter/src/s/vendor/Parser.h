
#pragma once

#include "Ast.h"
#include "Diag.h"
#include "Token.h"

#include <memory>
#include <string>
#include <vector>

namespace shalimar {

class Parser {
public:

    Parser(const std::vector<Token> &tokens, Diagnostics &diagnostics, int unit = 0);

    std::unique_ptr<Program> parse();

private:
    const std::vector<Token> &tokens_;
    Diagnostics &diag_;
    size_t index_ = 0;
    bool failed_ = false;
    int unit_ = 0;

    const Token &peek(size_t ahead = 0) const;
    const Token &current() const { return peek(0); }
    bool at(Tok kind) const { return current().kind == kind; }

    // A branch is written `else if`, in two words. There is no `elseif`: not
    // as a keyword, and not as a reserved name either - the word is an
    // ordinary identifier and a program may use it for one.
    bool atElseIf() const {
        return at(Tok::Else) && peek(1).kind == Tok::If;
    }
    bool atOperator(const char *spelling) const;
    const Token &advance();
    bool match(Tok kind);

    void fail(const std::string &text);
    void fail(int line, const std::string &text);
    bool expect(Tok kind, const std::string &text);

    std::string unexpected() const;
    void failUnexpected() { fail(unexpected()); }
    int lastLine() const;

    bool startsLine(size_t at) const;

    bool startsTerm() const;

    bool looksLikeNewStatement(size_t at) const;
    bool looksLikeMultiAssignHeader(size_t start) const;
    bool parenGroupHasTopLevelComma(size_t start) const;

    bool parsePrototype(Prototype &proto);
    bool parseUses(Program &program);
    std::unique_ptr<Function> parseFunction();
    Block parseBlock();
    StmtPtr parseStatement();
    StmtPtr parseStatementBody();
    StmtPtr parseDeclaration();
    StmtPtr parseAssignment();
    StmtPtr parsePrint();
    StmtPtr parseReturn();
    StmtPtr parseMultiAssign();
    StmtPtr parseIf();
    StmtPtr parseWhile();
    StmtPtr parseFor();

    int loopDepth_ = 0;


    bool atDeclaration() const;
    const Type *scalarTypeHere();

    ExprPtr parseExpression();
    ExprPtr parseOr();
    ExprPtr parseAnd();
    ExprPtr parseComparison();
    ExprPtr parseAdditive();
    ExprPtr parseMultiplicative();
    ExprPtr parsePower();
    ExprPtr parsePrimary();
    ExprPtr parsePostfix(ExprPtr base);
    ExprPtr parseInitializer();
    ExprPtr parseArrayLiteral();

    bool atPrecisionDirective() const;
};

}
