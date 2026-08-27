#ifndef C2S_CONVERT_STOC_H
#define C2S_CONVERT_STOC_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../c/CAst.h"
#include "../s/vendor/Ast.h"

namespace c2s {

class Source;
class Diagnostics;

class SToC : public shalimar::NodeVisitor {
public:
    SToC(const Source &source, Diagnostics &diagnostics);

    std::unique_ptr<CProgram> convert(shalimar::Program &program);

    int beyondCount() const { return beyondCount_; }

    std::string preamble() const;

    std::vector<std::string> includes() const;

    void visit(shalimar::IntLit &) override;
    void visit(shalimar::RealLit &) override;
    void visit(shalimar::Var &) override;
    void visit(shalimar::Convert &) override;
    void visit(shalimar::Binary &) override;
    void visit(shalimar::Declare &) override;
    void visit(shalimar::Assign &) override;
    void visit(shalimar::CompoundAssign &) override;
    void visit(shalimar::Print &) override;
    void visit(shalimar::If &) override;
    void visit(shalimar::While &) override;
    void visit(shalimar::For &) override;
    void visit(shalimar::Break &) override;
    void visit(shalimar::Continue &) override;
    void visit(shalimar::Call &) override;
    void visit(shalimar::Return &) override;
    void visit(shalimar::MultiAssign &) override;
    void visit(shalimar::CallStmt &) override;
    void visit(shalimar::StrLit &) override;
    void visit(shalimar::ArrayLit &) override;
    void visit(shalimar::Blank &) override;
    void visit(shalimar::Index &) override;
    void visit(shalimar::Dim &) override;
    void visit(shalimar::Precision &) override;

private:

    struct Info {
        std::string cName;
        int rank = 0;
        std::vector<long long> extents;
        bool isParamArray = false;
        bool isRefScalar = false;
    };

    void convertFunction(shalimar::Function &fn);
    void statement(shalimar::Stmt &node);
    CExprPtr expression(shalimar::Expr &node);
    void block(shalimar::Block &body, CCompound &into);
    void markBeyond(int line, const std::string &reason);

    const Info *infoFor(const shalimar::Symbol *symbol) const;
    const Info *lookupVar(const shalimar::Var &var);
    static std::string cEscape(const std::string &text);
    std::string freshName(const std::string &base);
    static std::string sanitise(const std::string &name);
    bool foldInt(shalimar::Expr &node, long long *out) const;
    CExprPtr linearIndex(shalimar::Expr &chain, int *outRankLeft,
                         const Info **outInfo);
    CExprPtr dimValue(const Info &info, int axis);
    CTypePtr scalarC(const shalimar::Type *type) const;
    CExprPtr callHelper(const std::string &name, std::vector<CExprPtr> args);
    void need(const std::string &helper);
    void printItem(shalimar::Expr &item);
    CStmtPtr declStmtFor(const std::string &name, CTypePtr type, CExprPtr init);

    const Source &source_;

    std::unique_ptr<CProgram> program_;
    CCompound *block_ = nullptr;
    CExprPtr expr_;
    shalimar::Function *currentFn_ = nullptr;
    shalimar::Program *sProgram_ = nullptr;

    std::map<const shalimar::Symbol *, Info> symbols_;
    std::map<std::string, Info> paramInfos_;
    std::map<std::string, bool> helpers_;
    int beyondCount_ = 0;
    int tempCount_ = 0;

    int currentLine_ = 0;

    bool usesPrint_ = false;
    bool usesMath_ = false;
    bool usesStdlib_ = false;
    bool currentIsMain_ = false;
};

}

#endif
