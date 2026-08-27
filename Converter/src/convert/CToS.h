#ifndef C2S_CONVERT_CTOS_H
#define C2S_CONVERT_CTOS_H

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "../Options.h"
#include "../c/CAst.h"
#include "../s/vendor/Ast.h"

namespace c2s {

class Source;
class Diagnostics;

class CToS : public CVisitor {
public:
    CToS(const Source &source, Diagnostics &diagnostics,
         const Permissions &permissions = Permissions());

    std::unique_ptr<shalimar::Program> convert(CProgram &program);

    int beyondCount() const { return beyondCount_; }

    std::string preamble() const { return std::string(); }

    void visit(CIntLit &) override;
    void visit(CFloatLit &) override;
    void visit(CCharLit &) override;
    void visit(CStringLit &) override;
    void visit(CIdent &) override;
    void visit(CUnary &) override;
    void visit(CBinary &) override;
    void visit(CAssign &) override;
    void visit(CTernary &) override;
    void visit(CCall &) override;
    void visit(CIndex &) override;
    void visit(CMember &) override;
    void visit(CCast &) override;
    void visit(CSizeof &) override;
    void visit(CComma &) override;
    void visit(CExprStmt &) override;
    void visit(CEmpty &) override;
    void visit(CCompound &) override;
    void visit(CIf &) override;
    void visit(CWhile &) override;
    void visit(CDoWhile &) override;
    void visit(CFor &) override;
    void visit(CSwitch &) override;
    void visit(CCase &) override;
    void visit(CBreak &) override;
    void visit(CContinue &) override;
    void visit(CReturn &) override;
    void visit(CGoto &) override;
    void visit(CLabel &) override;
    void visit(CDeclStmt &) override;
    void visit(CBeyond &) override;

private:

    // Kept, where it used to be taken and dropped: everything this class
    // cannot express goes into the file as a #BEYOND marker, but a thing it
    // *can* express with a difference has to be said out loud somewhere, and
    // that is a diagnostic.
    Diagnostics &diagnostics_;

    struct Info {
        std::string sName;
        const shalimar::Type *type = nullptr;
        int rank = 0;
        bool isChar = false;
    };

    void convertFunction(CFunctionDef &fn);
    void statement(CStmt &node);
    shalimar::ExprPtr expression(CExpr &node);
    void block(CStmt &node, shalimar::Block *into);
    void markBeyond(std::size_t offset, const std::string &reason);

    const shalimar::Type *scalarS(const CType &type, bool *lossy) const;
    // `asVariable` false for a function name. Shalimar 7.5.1 rule 3 takes a
    // borrowed name away from a VARIABLE only - a program's own function may
    // share the name and wins at the call - so renaming a function for a borrow
    // would be churn the reader has to reconcile against the C original.
    std::string rename(const std::string &name, bool asVariable = true);
    const Info *lookup(const std::string &name) const;
    void declareLocal(CDeclaration &decl, bool atTop);
    bool isPure(CExpr &node) const;

    void flushLifted();
    std::string mintLiftTemp(const shalimar::Type *type);
    bool lowerShortCircuit(CBinary &node);
    bool canLift_ = false;
    bool liftable_ = false;
    std::vector<shalimar::StmtPtr> lifted_;

    std::vector<std::pair<std::string, const shalimar::Type *> > liftTemps_;
    bool isCharContext(CExpr &other) const;

    bool isArrayValued(CExpr &node) const;
    bool isCharValued(CExpr &node) const;
    shalimar::ExprPtr charWrap(shalimar::ExprPtr value);
    shalimar::ExprPtr intWrap(shalimar::ExprPtr value);
    void lowerPrintf(CCall &call);

    std::string printFunction(const std::string &format,
                              const std::vector<shalimar::Param> &params,
                              shalimar::Block body, int line);

    bool lowerCountingFor(CFor &node, std::string *escapedCounter);
    bool counterEscapes(CFor &node, const std::string &name) const;

    bool isFileScope(const std::string &name) const;

    struct SwitchTemps {
        std::string selector;
        std::string entry;
        std::string done;
    };

    struct SwitchArm {
        std::vector<shalimar::ExprPtr> values;
        bool isDefault = false;
        std::vector<CStmt *> body;
        std::size_t offset = 0;
    };
    void lowerSwitch(CSwitch &node);

    void lowerFallingSwitchArms(CSwitch &node, const SwitchTemps &names,
                                std::vector<SwitchArm> &arms,
                                const std::vector<bool> &terminates,
                                bool wrapped);

    void closeSwitchWrapper(CSwitch &node, bool needsWrapper,
                            shalimar::Block *outerBlock,
                            shalimar::Block &wrapped);
    bool lowerTernaryReturn(CTernary &top, std::size_t offset,
                            shalimar::Block *into);
    bool returnArm(CExpr &value, std::size_t offset, shalimar::Block *into);
    void hoistDeclarations(CStmt &node, shalimar::Block *top);
    void convertTopDeclaration(CDeclaration &decl);
    std::size_t declOffset(CDeclaration &decl) const;
    int lineOf(std::size_t offset) const;
    std::vector<std::string> sourceLinesAt(std::size_t offset) const;

    const Source &source_;

    Permissions permissions_;

    std::unique_ptr<shalimar::Program> program_;
    shalimar::Block *block_ = nullptr;
    shalimar::ExprPtr expr_;
    bool currentIsMain_ = false;

    bool currentReturnsChar_ = false;
    CFunctionDef *currentFn_ = nullptr;
    int loopDepth_ = 0;

    std::vector<std::map<std::string, Info>> scopes_;

    std::map<std::size_t, Info> hoisted_;

    std::map<std::size_t, SwitchTemps> switchTemps_;
    // Folds `int r` + `r : 0` back into `int r : 0`, but only for the leading
    // statements of a function, which are the only ones that run exactly once
    // at entry the way a declaration's initialiser does.
    void foldOpeningAssignments(shalimar::Block &body);

    std::set<std::string> usedNames_;
    std::set<std::string> knownFunctions_;

    // The library names this file will borrow, collected BEFORE anything is
    // renamed. Shalimar's `uses` is per FILE while a C local is per function, so
    // a `sqrt()` call anywhere takes the name from every variable in the output -
    // and the walk that discovers the call may reach it long after the variable
    // has been named. See the scan in CToS.cpp.
    std::set<std::string> willBorrow_;

    std::map<std::string, std::string> printFunctions_;
    int printCount_ = 0;
    int beyondCount_ = 0;
    int tempCount_ = 0;
};

}

#endif
