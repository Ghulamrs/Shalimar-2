#ifndef C2S_S_SPRINTER_H
#define C2S_S_SPRINTER_H

#include <string>

#include "vendor/Ast.h"

namespace c2s {

class SPrinter : public shalimar::NodeVisitor {
public:
    SPrinter();

    std::string print(shalimar::Program &program);

    std::string printExpr(shalimar::Expr &expr);

    static std::string spellReal(double value);

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

    enum Tier {
        TierOr = 1,
        TierAnd = 2,
        TierComparison = 3,
        TierAdditive = 4,
        TierMultiplicative = 5,
        TierPower = 6,
        TierPrimary = 7
    };

    static Tier tierOf(shalimar::Binary::Op op);

    void expr(shalimar::Expr &node, int floor);
    void statement(shalimar::Stmt &node);
    void block(shalimar::Block &body);
    void functionBody(shalimar::Block &body);
    void functionHeader(const shalimar::Prototype &proto);
    void declare(shalimar::Declare &node);

    void indent();
    void line(const std::string &text);

    std::string out_;
    int depth_;

    int floor_;
};

}

#endif
