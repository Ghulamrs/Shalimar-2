#ifndef C2S_C_CPARSER_H
#define C2S_C_CPARSER_H

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "CAst.h"
#include "CToken.h"

namespace c2s {

class Source;
class Diagnostics;

class CParser {
public:
    CParser(const Source &source, std::vector<CToken> tokens,
            Diagnostics &diagnostics);

    std::unique_ptr<CProgram> parse();

private:

    const CToken &current() const;
    const CToken &peek(std::size_t ahead) const;
    void advance();
    bool at(const char *text) const;
    bool atKeyword(const char *text) const;
    bool accept(const char *text);
    bool expect(const char *text, const char *where);
    void fail(const std::string &message);
    bool failed() const { return failed_; }

    bool atTypeStart() const;
    bool isTypedefName(const std::string &name) const;

    struct Specifiers {
        CDeclaration::Storage storage = CDeclaration::Storage::None;
        CTypePtr type;
    };

    bool declarationSpecifiers(Specifiers *out, bool *sawAny);
    CTypePtr typeSpecifier(bool *sawType, bool *isConst, bool *isVolatile,
                           bool *isUnsigned, bool *isSignedWord,
                           bool *isShort, int *longCount);
    CTypePtr structOrUnion();
    CTypePtr enumSpecifier();

    bool declarator(CTypePtr base, std::string *name, CTypePtr *out,
                    bool abstractAllowed);
    bool directDeclarator(CTypePtr base, std::string *name, CTypePtr *out,
                          bool abstractAllowed);
    bool parameterList(CType *fn);
    CTypePtr typeName();

    std::unique_ptr<CDeclaration> declaration(Specifiers specifiers,
                                              bool *wasFunctionDef,
                                              std::unique_ptr<CFunctionDef> *fnOut);
    bool initializer(CInit *out);

    CStmtPtr statement();
    CStmtPtr compound();
    CStmtPtr blockItem();

    CExprPtr expression();
    CExprPtr assignment();
    CExprPtr conditional();
    CExprPtr binary(int minPrecedence);
    CExprPtr castExpression();
    CExprPtr unary();
    CExprPtr postfix();
    CExprPtr primary();

    const Source &source_;
    std::vector<CToken> tokens_;
    Diagnostics &diagnostics_;
    std::size_t index_ = 0;
    bool failed_ = false;

    std::vector<std::set<std::string>> typedefScopes_;
};

}

#endif
