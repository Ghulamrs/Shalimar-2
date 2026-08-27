
#pragma once

#include "Ast.h"
#include "Diag.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace shalimar {

class Scope {
public:
    void push() { levels_.push_back(Level()); }
    void pop() { levels_.pop_back(); }
    void clear() { levels_.clear(); }

    void define(const std::string &name, const Symbol *symbol) {
        levels_.back()[name] = symbol;
    }

    const Symbol *lookup(const std::string &name) const {
        for (size_t i = levels_.size(); i-- > 0;) {
            Level::const_iterator found = levels_[i].find(name);
            if (found != levels_[i].end()) return found->second;
        }
        return nullptr;
    }

    bool definedHere(const std::string &name) const {
        return !levels_.empty() && levels_.back().count(name) != 0;
    }

private:
    using Level = std::map<std::string, const Symbol *>;
    std::vector<Level> levels_;
};

class Checker : public NodeVisitor {
public:
    explicit Checker(Diagnostics &diagnostics) : diag_(diagnostics) {}

    bool check(Program &program);

    void visit(IntLit &node) override;
    void visit(RealLit &node) override;
    void visit(StrLit &node) override;
    void visit(ArrayLit &node) override;
    void visit(Blank &node) override;
    void visit(Var &node) override;
    void visit(Index &node) override;
    void visit(Dim &node) override;
    void visit(Precision &node) override;
    void visit(Convert &node) override;
    void visit(Binary &node) override;
    void visit(Call &node) override;

    void visit(Declare &node) override;
    void visit(Assign &node) override;
    void visit(CompoundAssign &node) override;
    void visit(MultiAssign &node) override;
    void visit(CallStmt &node) override;
    void visit(Return &node) override;
    void visit(Print &node) override;
    void visit(If &node) override;
    void visit(While &node) override;
    void visit(For &node) override;
    void visit(Break &node) override;
    void visit(Continue &node) override;

private:
    Diagnostics &diag_;
    Program *program_ = nullptr;
    int line_ = 0;
    int unit_ = 0;
    Function *function_ = nullptr;
    Scope scope_;
    int strings_ = 0;

    std::map<std::string, const Symbol *> globals_;
    std::map<std::string, int> laterGlobals_;
    bool inGlobalScope_ = false;

    // Every name the function being checked has DECLARED, at any depth, kept for the
    // whole function rather than popped with its block. A declaration may sit in a
    // block now, but a declared local is still the whole call's - one name, one
    // variable, one type - so two sibling blocks may not each declare 't'. `scope_`
    // cannot answer that: the first block's level is popped long before the second is
    // read. Names made by a first assignment are NOT in here; those belong to their
    // block and always have.
    std::set<std::string> declaredLocals_;

    Symbol *declareName(const std::string &name, const Type *type);
    const Symbol *lookup(const std::string &name) const;
    void reportUndefined(const std::string &name);

    void check(Function &function);
    void check(Stmt &statement);

    const Type *typeOf(ExprPtr &expr);
    void coerce(ExprPtr &expr, const Type *to);
    const Type *common(const Type *a, const Type *b) const;

    void checkCondition(ExprPtr &expr);
    void checkBlock(Block &body);

    const Type *literalType(ArrayLit &node);
    void coerceLiteral(ArrayLit &node, const Type *arrayType);

    bool constantNumber(const Expr &expr, double &value) const;
    void warnIfLoopNeverRuns(For &node);
    static std::string number(double value);
    static bool alwaysReturns(const Block &body);

    bool refuseConstant(const std::string &name, const char *what);
    bool refuseBorrowed(const std::string &name, int line);

};

}
