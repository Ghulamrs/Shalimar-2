
#pragma once

#include "Type.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace shalimar {

class IntLit;
class RealLit;
class Var;
class Convert;
class Binary;
class Declare;
class Assign;
class CompoundAssign;
class Print;
class If;
class While;
class For;
class Break;
class Continue;
class Call;
class Return;
class MultiAssign;
class CallStmt;
class StrLit;
class ArrayLit;
class Blank;
class Index;
class Dim;
class Precision;

class NodeVisitor {
public:
    virtual ~NodeVisitor() = default;

    virtual void visit(IntLit &) = 0;
    virtual void visit(RealLit &) = 0;
    virtual void visit(Var &) = 0;
    virtual void visit(Convert &) = 0;
    virtual void visit(Binary &) = 0;
    virtual void visit(Declare &) = 0;
    virtual void visit(Assign &) = 0;
    virtual void visit(CompoundAssign &) = 0;
    virtual void visit(Print &) = 0;
    virtual void visit(If &) = 0;
    virtual void visit(While &) = 0;
    virtual void visit(For &) = 0;
    virtual void visit(Break &) = 0;
    virtual void visit(Continue &) = 0;
    virtual void visit(Call &) = 0;
    virtual void visit(Return &) = 0;
    virtual void visit(MultiAssign &) = 0;
    virtual void visit(CallStmt &) = 0;
    virtual void visit(StrLit &) = 0;
    virtual void visit(ArrayLit &) = 0;
    virtual void visit(Blank &) = 0;
    virtual void visit(Index &) = 0;
    virtual void visit(Dim &) = 0;
    virtual void visit(Precision &) = 0;
};

class Node {
public:
    virtual ~Node() = default;
    virtual void accept(NodeVisitor &v) = 0;

protected:
    Node() = default;
};

class Symbol {
public:

    enum class Storage { Local, Global };

    Symbol(std::string name, const Type *type, int slot, Storage storage = Storage::Local)
        : name_(std::move(name)), type_(type), slot_(slot), storage_(storage) {}

    const std::string &name() const { return name_; }
    const Type *type() const { return type_; }
    int slot() const { return slot_; }
    Storage storage() const { return storage_; }
    bool isGlobal() const { return storage_ == Storage::Global; }

    bool isReference() const { return reference_; }
    void makeReference() { reference_ = true; }

private:
    std::string name_;
    const Type *type_;
    int slot_;
    Storage storage_;
    bool reference_ = false;
};

class Expr : public Node {
public:

    const Type *type() const { return type_; }
    void setType(const Type *t) { type_ = t; }

    virtual bool isAddressable() const { return false; }

    virtual bool isIntLiteral() const { return false; }
    virtual bool isRealLiteral() const { return false; }

private:
    const Type *type_ = nullptr;
};

using ExprPtr = std::unique_ptr<Expr>;

class IntLit : public Expr {
public:
    explicit IntLit(int32_t value) : value_(value) {}

    int32_t value() const { return value_; }
    bool isIntLiteral() const override { return true; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    int32_t value_;
};

class RealLit : public Expr {
public:
    explicit RealLit(double value) : value_(value) {}

    double value() const { return value_; }
    bool isRealLiteral() const override { return true; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    double value_;
};

class Var : public Expr {
public:
    explicit Var(std::string name) : name_(std::move(name)) {}

    const std::string &name() const { return name_; }
    const Symbol *symbol() const { return symbol_; }
    void resolve(const Symbol *s) { symbol_ = s; }

    bool isNamedConstant() const { return constant_; }
    double constant() const { return value_; }
    void resolveConstant(double value) { constant_ = true; value_ = value; }

    bool isAddressable() const override { return !constant_; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::string name_;
    const Symbol *symbol_ = nullptr;
    bool constant_ = false;
    double value_ = 0.0;
};

class Convert : public Expr {
public:
    Convert(ExprPtr expr, const Type *to) : expr_(std::move(expr)) { setType(to); }

    Expr &expr() const { return *expr_; }
    ExprPtr &operand() { return expr_; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr expr_;
};

class Binary : public Expr {
public:
    enum class Op {
        Add, Subtract, Multiply, Divide, Modulus, Power,
        Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual,
        And, Or
    };

    Binary(Op op, ExprPtr lhs, ExprPtr rhs)
        : op_(op), lhs_(std::move(lhs)), rhs_(std::move(rhs)) {}

    Op op() const { return op_; }
    Expr &lhs() const { return *lhs_; }
    Expr &rhs() const { return *rhs_; }
    ExprPtr &left() { return lhs_; }
    ExprPtr &right() { return rhs_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

    static const char *spelling(Op op);

    static bool yieldsInt(Op op);

    static const char *runtimeFor(Op op, const Type *operands);

private:
    Op op_;
    ExprPtr lhs_;
    ExprPtr rhs_;
};

class StrLit : public Expr {
public:
    explicit StrLit(std::string text) : text_(std::move(text)) {}

    const std::string &text() const { return text_; }
    int id() const { return id_; }
    void setId(int id) { id_ = id; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::string text_;
    int id_ = 0;
};

class ArrayLit : public Expr {
public:
    void add(ExprPtr element) { elements_.push_back(std::move(element)); }
    std::vector<ExprPtr> &elements() { return elements_; }
    const std::vector<ExprPtr> &elements() const { return elements_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::vector<ExprPtr> elements_;
};

class Blank : public Expr {
public:
    void accept(NodeVisitor &v) override { v.visit(*this); }
};

class Index : public Expr {
public:
    Index(ExprPtr base, ExprPtr index)
        : base_(std::move(base)), index_(std::move(index)) {}

    Expr &base() const { return *base_; }
    Expr &index() const { return *index_; }
    ExprPtr &baseRef() { return base_; }
    ExprPtr &indexRef() { return index_; }

    bool isAddressable() const override { return true; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr base_;
    ExprPtr index_;
};

class Dim : public Expr {
public:
    Dim(ExprPtr base, ExprPtr axis, std::string spelling)
        : base_(std::move(base)), axis_(std::move(axis)), spelling_(std::move(spelling)) {}

    ExprPtr &base() { return base_; }
    ExprPtr &axis() { return axis_; }
    const std::string &spelling() const { return spelling_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr base_;
    ExprPtr axis_;
    std::string spelling_;
};

class Precision : public Expr {
public:
    explicit Precision(ExprPtr places) : places_(std::move(places)) {}

    ExprPtr &places() { return places_; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr places_;
};

struct Prototype;

class Call : public Expr {
public:
    Call(std::string callee, int line) : callee_(std::move(callee)), line_(line) {}

    int builtin() const { return builtin_; }
    void resolveBuiltin(int index) { builtin_ = index; }

    void add(ExprPtr argument) { arguments_.push_back(std::move(argument)); }

    const std::string &callee() const { return callee_; }
    std::vector<ExprPtr> &arguments() { return arguments_; }
    int line() const { return line_; }

    const Prototype *prototype() const { return prototype_; }
    void resolve(const Prototype *p) { prototype_ = p; }

    int scratchBase() const { return scratchBase_; }
    void setScratchBase(int base) { scratchBase_ = base; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::string callee_;
    std::vector<ExprPtr> arguments_;
    int line_;
    const Prototype *prototype_ = nullptr;
    int scratchBase_ = 0;
    int builtin_ = -1;
};

class Stmt : public Node {
public:
    int line() const { return line_; }

    int unit() const { return unit_; }
    void setUnit(int unit) { unit_ = unit; }

protected:
    explicit Stmt(int line) : line_(line) {}

private:
    int line_;
    int unit_ = 0;
};

using StmtPtr = std::unique_ptr<Stmt>;
using Block = std::vector<StmtPtr>;

class Declare : public Stmt {
public:
    Declare(const Type *type, std::string name, ExprPtr initial, int line)
        : Stmt(line), type_(type), name_(std::move(name)), initial_(std::move(initial)) {}

    void addExtent(ExprPtr extent) { extents_.push_back(std::move(extent)); }
    std::vector<ExprPtr> &extents() { return extents_; }

    int extentBase() const { return extentBase_; }
    void setExtentBase(int base) { extentBase_ = base; }

    const Type *declaredType() const { return type_; }
    void setDeclaredType(const Type *type) { type_ = type; }
    const std::string &name() const { return name_; }
    ExprPtr &initial() { return initial_; }
    const Symbol *symbol() const { return symbol_; }
    void resolve(const Symbol *s) { symbol_ = s; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    const Type *type_;
    std::string name_;
    std::vector<ExprPtr> extents_;
    ExprPtr initial_;
    const Symbol *symbol_ = nullptr;
    int extentBase_ = 0;
};

class Assign : public Stmt {
public:
    Assign(ExprPtr target, ExprPtr expr, int line)
        : Stmt(line), target_(std::move(target)), expr_(std::move(expr)) {}

    ExprPtr &target() { return target_; }
    ExprPtr &expr() { return expr_; }
    const Symbol *symbol() const { return symbol_; }
    void resolve(const Symbol *s) { symbol_ = s; }

    bool creates() const { return creates_; }
    void setCreates(bool value) { creates_ = value; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr target_;
    ExprPtr expr_;
    const Symbol *symbol_ = nullptr;
    bool creates_ = false;
};

class CompoundAssign : public Stmt {
public:
    CompoundAssign(ExprPtr target, bool add, ExprPtr expr, int line)
        : Stmt(line), target_(std::move(target)), expr_(std::move(expr)), add_(add) {}

    ExprPtr &target() { return target_; }
    ExprPtr &expr() { return expr_; }
    bool isAdd() const { return add_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr target_;
    ExprPtr expr_;
    bool add_;
};

class Print : public Stmt {
public:
    Print(bool newline, int line) : Stmt(line), newline_(newline) {}

    void add(ExprPtr item) { items_.push_back(std::move(item)); }

    std::vector<ExprPtr> &items() { return items_; }
    const std::vector<ExprPtr> &items() const { return items_; }
    bool newline() const { return newline_; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::vector<ExprPtr> items_;
    bool newline_;
};

class Return : public Stmt {
public:
    explicit Return(int line) : Stmt(line) {}

    void add(ExprPtr expr) { exprs_.push_back(std::move(expr)); }
    std::vector<ExprPtr> &exprs() { return exprs_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::vector<ExprPtr> exprs_;
};

class MultiAssign : public Stmt {
public:
    MultiAssign(int line) : Stmt(line) {}

    void addTarget(std::string name) { names_.push_back(std::move(name)); }
    void setCall(ExprPtr call) { call_ = std::move(call); }

    const std::vector<std::string> &names() const { return names_; }
    std::vector<const Symbol *> &targets() { return targets_; }
    ExprPtr &call() { return call_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::vector<std::string> names_;
    std::vector<const Symbol *> targets_;
    ExprPtr call_;
};

class CallStmt : public Stmt {
public:
    CallStmt(ExprPtr call, int line) : Stmt(line), call_(std::move(call)) {}

    ExprPtr &call() { return call_; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr call_;
};

class If : public Stmt {
public:
    struct Branch {
        ExprPtr condition;
        Block body;
    };

    explicit If(int line) : Stmt(line) {}

    void addBranch(ExprPtr condition, Block body) {
        branches_.push_back(Branch());
        branches_.back().condition = std::move(condition);
        branches_.back().body = std::move(body);
    }
    void setElse(Block body) { elseBody_ = std::move(body); hasElse_ = true; }

    std::vector<Branch> &branches() { return branches_; }
    Block &elseBody() { return elseBody_; }
    bool hasElse() const { return hasElse_; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::vector<Branch> branches_;
    Block elseBody_;
    bool hasElse_ = false;
};

class While : public Stmt {
public:
    While(ExprPtr condition, Block body, int line)
        : Stmt(line), condition_(std::move(condition)), body_(std::move(body)) {}

    ExprPtr &condition() { return condition_; }
    Block &body() { return body_; }
    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    ExprPtr condition_;
    Block body_;
};

class For : public Stmt {
public:
    For(std::string variable, ExprPtr start, ExprPtr end, ExprPtr step, Block body, int line)
        : Stmt(line), variable_(std::move(variable)), start_(std::move(start)),
          end_(std::move(end)), step_(std::move(step)), body_(std::move(body)) {}

    const std::string &variable() const { return variable_; }
    ExprPtr &start() { return start_; }
    ExprPtr &end() { return end_; }
    ExprPtr &step() { return step_; }
    Block &body() { return body_; }

    const Symbol *counter() const { return counter_; }
    void resolve(const Symbol *s) { counter_ = s; }

    enum HiddenSlot { EndSlot, StepSlot, PassSlot, StartSlot, HiddenSlotCount };
    int hidden(HiddenSlot which) const { return hiddenBase_ + which; }
    void setHiddenBase(int base) { hiddenBase_ = base; }

    void accept(NodeVisitor &v) override { v.visit(*this); }

private:
    std::string variable_;
    ExprPtr start_;
    ExprPtr end_;
    ExprPtr step_;
    Block body_;
    const Symbol *counter_ = nullptr;
    int hiddenBase_ = 0;
};

class Break : public Stmt {
public:
    explicit Break(int line) : Stmt(line) {}
    void accept(NodeVisitor &v) override { v.visit(*this); }
};

class Continue : public Stmt {
public:
    explicit Continue(int line) : Stmt(line) {}
    void accept(NodeVisitor &v) override { v.visit(*this); }
};

class Frame {
public:
    static const int slotBytes = 8;

    int addVariable() { return variables_++; }

    int variables() const { return variables_; }

    int evaluationBase() const { return variables_; }

private:
    int variables_ = 0;
};

struct Param {
    std::string name;
    const Type *type = nullptr;
    bool byReference = false;
};

struct Prototype {
    Prototype() = default;
    Prototype(std::string n, int l) : name(std::move(n)), line(l) {}

    std::string name;
    std::vector<const Type *> outputs;
    std::vector<Param> inputs;
    int line = 0;

    int id = 0;

    int unit = 0;

    // Declared with `uses`, defined by whatever the link is given. Its name
    // is NOT mangled: shmf_ marks a function this compiler wrote, and this is
    // somebody else's, called by the name their C gave it.
    bool isForeign = false;

    bool returnsByPointer() const { return outputs.size() > 1; }
};

class Function {
public:
    Function(Prototype proto, Block body)
        : proto_(std::move(proto)), body_(std::move(body)) {}

    const Prototype &proto() const { return proto_; }
    Prototype &proto() { return proto_; }
    bool isCalled() const { return called_; }
    void markCalled() { called_ = true; }

    bool isRejected() const { return rejected_; }
    void reject() { rejected_ = true; }
    Block &body() { return body_; }
    const Block &body() const { return body_; }
    Frame &frame() { return frame_; }
    const Frame &frame() const { return frame_; }

    Symbol *declare(const std::string &name, const Type *type) {
        symbols_.push_back(std::unique_ptr<Symbol>(
            new Symbol(name, type, frame_.addVariable())));
        return symbols_.back().get();
    }

    int addHiddenSlot() { return frame_.addVariable(); }

    int outPointerBase() const { return outPointerBase_; }
    void setOutPointerBase(int base) { outPointerBase_ = base; }

    int resultSlot() const { return resultSlot_; }
    void setResultSlot(int slot) { resultSlot_ = slot; }

private:
    Prototype proto_;
    Block body_;
    Frame frame_;
    std::vector<std::unique_ptr<Symbol>> symbols_;
    bool called_ = false;
    bool rejected_ = false;
    int outPointerBase_ = 0;
    int resultSlot_ = -1;
};

class Program {
public:

    void add(std::unique_ptr<Function> f) {
        order_.push_back(Entry{true, functions_.size()});
        functions_.push_back(std::move(f));
    }

    void addGlobal(StmtPtr declaration) {
        order_.push_back(Entry{false, globals_.size()});
        globals_.push_back(std::move(declaration));
    }

    struct Entry { bool isFunction; size_t index; };

    // What this file borrows from the C library. A name and the line that
    // asked for it, kept in order so a diagnostic can point at the right one.
    // Per file, per CROSSFILE.md rule 1 - what a file depends on travels with
    // it - so this belongs to the unit, not to the whole program.
    // `own` says whether THIS file's own `uses` asked for it, or whether Resolve
    // brought it in with a function pulled from another file. Both make the name
    // callable; only the file's own borrow takes the name away from a variable
    // (FOREIGN.md rule 3), because that rule is per file like the clause is. Without
    // the distinction, a `uses fmod` in a file you merely call into would refuse
    // `fmod` as a variable HERE - and the app, which has one file and no merging,
    // would disagree about which programs are legal.
    struct Borrowed { std::string name; int line; bool own; };

    // A function declared with `uses <real> = f(...)` and defined somewhere
    // else entirely - a library the link is given. Its prototype is the only
    // thing this compiler will ever know about it, which is why the
    // declaration carries one and the table form does not.
    void declareForeign(Prototype proto) { foreign_.push_back(std::move(proto)); }
    std::vector<Prototype> &foreign() { return foreign_; }
    const std::vector<Prototype> &foreign() const { return foreign_; }
    const Prototype *foreignNamed(const std::string &name) const {
        for (std::size_t i = 0; i < foreign_.size(); ++i)
            if (foreign_[i].name == name) return &foreign_[i];
        return nullptr;
    }
    void borrow(const std::string &name, int line, bool own = true) {
        borrowed_.push_back(Borrowed{name, line, own});
    }

    // The line this file's OWN clause asked for it on, or 0 if the name is not
    // borrowed by this file at all - a merged borrow answers 0 like an absent one.
    int borrowedOwnOn(const std::string &name) const {
        for (std::size_t i = 0; i < borrowed_.size(); ++i)
            if (borrowed_[i].own && borrowed_[i].name == name) return borrowed_[i].line;
        return 0;
    }
    const std::vector<Borrowed> &borrowed() const { return borrowed_; }

    // Did this file ask for that name? A library function is only a library
    // function where it was borrowed; everywhere else it is an ordinary
    // identifier, which is the whole point of `uses`.
    bool borrows(const std::string &name) const {
        for (std::size_t i = 0; i < borrowed_.size(); ++i)
            if (borrowed_[i].name == name) return true;
        return false;
    }

    std::vector<std::unique_ptr<Function>> &functions() { return functions_; }
    const std::vector<std::unique_ptr<Function>> &functions() const { return functions_; }
    std::vector<StmtPtr> &globals() { return globals_; }
    const std::vector<Entry> &order() const { return order_; }

    const Function *find(const std::string &name) const;
    Function *find(const std::string &name);

    int addGlobalSlot() { return globalSlots_++; }
    int globalSlots() const { return globalSlots_; }

    Function &initializer() { return initializer_; }

private:
    std::vector<std::unique_ptr<Function>> functions_;
    std::vector<StmtPtr> globals_;
    std::vector<Entry> order_;
    std::vector<Borrowed> borrowed_;
    std::vector<Prototype> foreign_;
    int globalSlots_ = 0;
    Function initializer_{Prototype(), Block()};
};

}
