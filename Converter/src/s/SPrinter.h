#ifndef C2S_S_SPRINTER_H
#define C2S_S_SPRINTER_H

#include <string>
#include <vector>

#include "vendor/Ast.h"

namespace c2s {

class SPrinter : public shalimar::NodeVisitor {
public:
    SPrinter();

    std::string print(shalimar::Program &program);

    std::string printExpr(shalimar::Expr &expr);

    /// The source line each printed line came from, indexed by printed line
    /// minus one - so `lineMap()[0]` is where the first line of the output was
    /// written. Valid after `print`, and empty until then.
    ///
    /// **The numbers are the input's, not this printer's.** Every statement in
    /// the tree carries the line of the construct it was built from, and for a
    /// converted program that is a line of the *C*. So this is what turns a
    /// complaint about the emitted Shalimar back into a place in the file its
    /// author is looking at, which is the only file they can act on.
    ///
    /// 0 where no statement owns the line: the `uses` clause, the blank line
    /// between two functions, anything a caller prepended.
    const std::vector<int> &lineMap() const { return map_; }

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

    void sync();

    std::string out_;
    int depth_;

    // The map, and the two pieces of bookkeeping that fill it: how far into
    // out_ the newlines have been counted, and which source line the text
    // being written now belongs to.
    std::vector<int> map_;
    std::size_t scanned_;
    int mapLine_;

    int floor_;
};

}

#endif
