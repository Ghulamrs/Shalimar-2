#include "Check.h"

#include "Builtin.h"

namespace shalimar {
namespace {

const Type *charArray() { return Type::arrayOf(Type::charType()); }

bool isText(const Type *type) { return type == charArray(); }

class CallCollector : public NodeVisitor {
public:
    std::vector<std::string> names;

    void walk(Block &body) { for (StmtPtr &s : body) s->accept(*this); }
    void walk(ExprPtr &e) { if (e) e->accept(*this); }

    void visit(IntLit &) override {}
    void visit(RealLit &) override {}
    void visit(StrLit &) override {}
    void visit(Blank &) override {}
    void visit(Var &) override {}
    void visit(Break &) override {}
    void visit(Continue &) override {}

    void visit(ArrayLit &n) override { for (ExprPtr &e : n.elements()) walk(e); }
    void visit(Index &n) override { walk(n.baseRef()); walk(n.indexRef()); }
    void visit(Dim &n) override { walk(n.base()); walk(n.axis()); }
    void visit(Precision &n) override { walk(n.places()); }
    void visit(Convert &n) override { walk(n.operand()); }
    void visit(Binary &n) override { walk(n.left()); walk(n.right()); }

    void visit(Call &n) override {
        names.push_back(n.callee());
        for (ExprPtr &e : n.arguments()) walk(e);
    }

    void visit(Declare &n) override {
        for (ExprPtr &e : n.extents()) walk(e);
        walk(n.initial());
    }
    void visit(Assign &n) override { walk(n.target()); walk(n.expr()); }
    void visit(CompoundAssign &n) override { walk(n.target()); walk(n.expr()); }
    void visit(MultiAssign &n) override { walk(n.call()); }
    void visit(CallStmt &n) override { walk(n.call()); }
    void visit(Return &n) override { for (ExprPtr &e : n.exprs()) walk(e); }
    void visit(Print &n) override { for (ExprPtr &e : n.items()) walk(e); }
    void visit(If &n) override {
        for (If::Branch &b : n.branches()) { walk(b.condition); walk(b.body); }
        if (n.hasElse()) walk(n.elseBody());
    }
    void visit(While &n) override { walk(n.condition()); walk(n.body()); }
    void visit(For &n) override {
        walk(n.start()); walk(n.end()); walk(n.step()); walk(n.body());
    }
};

}

Symbol *Checker::declareName(const std::string &name, const Type *type) {
    if (!inGlobalScope_) return function_->declare(name, type);
    Symbol *symbol = new Symbol(name, type, program_->addGlobalSlot(), Symbol::Storage::Global);
    globals_[name] = symbol;
    laterGlobals_.erase(name);
    return symbol;
}

const Symbol *Checker::lookup(const std::string &name) const {
    if (const Symbol *local = scope_.lookup(name)) return local;
    std::map<std::string, const Symbol *>::const_iterator found = globals_.find(name);
    return found == globals_.end() ? nullptr : found->second;
}

void Checker::reportUndefined(const std::string &name) {
    std::map<std::string, int>::const_iterator later = laterGlobals_.find(name);
    if (later != laterGlobals_.end()) {
        diag_.error(unit_, line_, "'" + name + "' is a global declared later, on line " +
                               std::to_string(later->second));
        return;
    }
    diag_.error(unit_, line_, "Undefined variable '" + name + "'");
}

bool Checker::check(Program &program) {
    program_ = &program;

    // What the file said it borrows, checked before anything uses it, so that
    // a name it cannot have is reported where it was asked for rather than at
    // the call - which may be pages away, or in another file entirely.
    //
    // Borrowing something and never calling it is not an error: it costs
    // nothing and emits nothing, and a file that borrows a set for the project
    // it belongs to should not be nagged about the ones it did not reach for.
    // docs/FOREIGN.md, rule 4.
    // A foreign declaration must describe something a C function can actually
    // be. Two outputs cannot: shc returns them through a scratch block whose
    // address it passes in a register of its own choosing - a convention that
    // is fine while both ends are code this compiler wrote, and is not written
    // down anywhere for anybody else to implement. It parses, emits and links,
    // and would simply be wrong, which is the worst of the four.
    //
    // A C function returning two values does it through a pointer parameter,
    // and Shalimar has no pointer type - so there is no spelling of this that
    // would work, and refusing is the whole answer rather than a limitation to
    // be lifted later.
    for (std::size_t i = 0; i < program.foreign().size(); ++i) {
        const Prototype &f = program.foreign()[i];
        if (f.outputs.size() > 1) {
            diag_.error(unit_, f.line,
                        "'" + f.name + "' is declared with " +
                        std::to_string(f.outputs.size()) +
                        " outputs; a library function may answer at most one");
        }
    }

    for (std::size_t i = 0; i < program.borrowed().size(); ++i) {
        const Program::Borrowed &b = program.borrowed()[i];
        if (findBuiltin(b.name) >= 0) continue;

        if (const char *why = whyNotBorrowable(b.name)) {
            diag_.error(unit_, b.line, "'" + b.name + "' " + why);
        } else {
            diag_.error(unit_, b.line,
                        "'" + b.name + "' is not a library function this compiler knows");
        }
    }

    for (StmtPtr &s : program.globals()) {
        Declare &declaration = static_cast<Declare &>(*s);
        if (!laterGlobals_.count(declaration.name())) {
            laterGlobals_[declaration.name()] = declaration.line();
        }
    }

    int id = 0;
    for (std::unique_ptr<Function> &f : program.functions()) {
        f->proto().id = id++;
        if (f->proto().name == "main" && !f->proto().inputs.empty()) {
            diag_.error(f->proto().unit, f->proto().line, "main() takes no inputs");
        }
        if (Function *existing = program.find(f->proto().name)) {
            if (existing != f.get()) {
                diag_.error(f->proto().unit, f->proto().line, "Function '" + f->proto().name +
                                                 "' already defined (line " +
                                                 std::to_string(existing->proto().line) + ")");
                f->reject();
                continue;
            }
        }
        if (f->proto().name == "prec") {
            diag_.error(f->proto().unit, f->proto().line, "'prec' is reserved for '? prec(n)'");
            f->reject();
            continue;
        }
    }

    Function *entry = program.find("main");
    if (!entry) diag_.error(0, "No main() function defined");
    else entry->markCalled();

    {
        CallCollector collector;
        for (std::unique_ptr<Function> &f : program.functions()) collector.walk(f->body());
        for (StmtPtr &g : program.globals()) g->accept(collector);
        for (const std::string &name : collector.names) {
            if (Function *called = program.find(name)) called->markCalled();
        }
    }
    for (std::unique_ptr<Function> &f : program.functions()) {
        if (!f->isCalled() && !f->isRejected()) {
            diag_.warning(f->proto().unit, f->proto().line, "'" + f->proto().name + "' is never called");
        }
    }

    for (const Program::Entry &entry_ : program.order()) {
        if (entry_.isFunction) {
            check(*program.functions()[entry_.index]);
            continue;
        }
        inGlobalScope_ = true;
        function_ = &program.initializer();
        scope_.clear();
        scope_.push();
        check(*program.globals()[entry_.index]);
        scope_.pop();
        function_ = nullptr;
        inGlobalScope_ = false;
    }

    return !diag_.hasErrors();
}

void Checker::check(Function &function) {
    function_ = &function;
    unit_ = function.proto().unit;
    scope_.clear();
    scope_.push();
    declaredLocals_.clear();

    for (const Param &parameter : function.proto().inputs) {
        refuseBorrowed(parameter.name, function.proto().line);
        Symbol *symbol = function.declare(parameter.name, parameter.type);
        if (parameter.byReference) symbol->makeReference();
        scope_.define(parameter.name, symbol);
    }
    if (function.proto().returnsByPointer()) {
        const int base = function.addHiddenSlot();
        for (size_t i = 1; i < function.proto().outputs.size(); ++i) function.addHiddenSlot();
        function.setOutPointerBase(base);
    } else if (function.proto().outputs.size() == 1) {
        function.setResultSlot(function.addHiddenSlot());
    }

    for (StmtPtr &s : function.body()) check(*s);

    if (!function.proto().outputs.empty() && !alwaysReturns(function.body())) {
        diag_.error(function.proto().unit, function.proto().line,
                    "'" + function.proto().name + "' can finish without a return");
    }

    scope_.pop();
    function_ = nullptr;
}

bool Checker::alwaysReturns(const Block &body) {
    for (const StmtPtr &s : body) {
        if (dynamic_cast<const Return *>(s.get())) return true;
        If *branch = const_cast<If *>(dynamic_cast<const If *>(s.get()));
        if (!branch || !branch->hasElse()) continue;
        bool all = true;
        for (If::Branch &arm : branch->branches()) {
            if (!alwaysReturns(arm.body)) { all = false; break; }
        }
        if (all && alwaysReturns(branch->elseBody())) return true;
    }
    return false;
}

void Checker::check(Stmt &statement) {
    line_ = statement.line();
    unit_ = statement.unit();
    statement.accept(*this);
}

const Type *Checker::typeOf(ExprPtr &expr) {
    expr->accept(*this);
    return expr->type();
}

void Checker::coerce(ExprPtr &expr, const Type *to) {
    const Type *from = expr->type();
    if (!to || !from || from == to) return;
    if (from->isArray() || to->isArray()) {
        diag_.error(unit_, line_, "Cannot use " + from->spelling() + " where " +
                               to->spelling() + " is required");
        return;
    }
    const bool numeric = (from == Type::intType() || from == Type::realType()) &&
                         (to == Type::intType() || to == Type::realType());
    if (!numeric) {
        diag_.error(unit_, line_, "Cannot use " + from->spelling() + " where " +
                               to->spelling() + " is required");
        return;
    }
    ExprPtr inner(expr.release());
    expr.reset(new Convert(std::move(inner), to));
}

const Type *Checker::common(const Type *a, const Type *b) const {
    if (!a || !b) return nullptr;
    if (a == b) return a;
    if (a == Type::intType() && b == Type::realType()) return Type::realType();
    if (a == Type::realType() && b == Type::intType()) return Type::realType();
    return a;
}

bool Checker::refuseConstant(const std::string &name, const char *what) {
    // A program may have its own pi or e, but it has to say so. Declared -
    // 'real pi', or a parameter - the name is the program's for that whole
    // body and the constant is simply not in it. Created by assignment it is
    // refused, because Shalimar makes a name on first write: 'pi : 3' would
    // leave '? pi' meaning 3.14159 above the line and 3 below it, one name
    // with two meanings in one function. That is the hazard
    // SHALIMAR_LANGUAGE.md named when it made these read-only, and it is the
    // half worth keeping.
    if (!isConstant(name)) return false;
    if (lookup(name) != nullptr) return false;
    (void)what;
    diag_.error(unit_, line_, "'" + name + "' is a constant - declare it first "
                              "if you want your own");
    return true;
}

// **A borrowed name may not also be a variable** - FOREIGN.md rule 3. `fmod` is an
// ordinary identifier in every file that does not borrow it; in one that does, the
// name is spoken for, and `real fmod` beside `fmod(7.5, 2.0)` would be one name
// meaning two things in one file. That is the hazard the language already named when
// it refused `pi : 3`.
//
// Stricter than a constant, which may be had by declaring it: there is no declaring
// your way out of a borrow, because the clause has already claimed the name for the
// file. The message names both lines, since the cure is at one or the other.
//
// **This file's own borrows only.** Resolve merges the borrows of any file it pulls
// a function from, so that the pulled function's calls resolve; those must not take
// a name away from a variable here. Ast.h's `own` flag is what tells them apart.
//
// Every caller reports and CARRIES ON rather than returning, so the name still
// enters scope and a later mention resolves. Bailing produced "'fmod' is borrowed"
// followed by "Undefined variable 'fmod'" at a line that is not the mistake.
bool Checker::refuseBorrowed(const std::string &name, int line) {
    if (program_ == nullptr) return false;
    const int asked = program_->borrowedOwnOn(name);
    if (asked == 0) return false;
    diag_.error(unit_, line, "'" + name + "' is borrowed on line " + std::to_string(asked) +
                             " - drop the borrow or use another name");
    return true;
}

void Checker::visit(IntLit &node) { node.setType(Type::intType()); }
void Checker::visit(RealLit &node) { node.setType(Type::realType()); }

void Checker::visit(StrLit &node) {
    node.setId(strings_++);
    node.setType(charArray());
}

void Checker::visit(Blank &node) {

    node.setType(nullptr);
}

const Type *Checker::literalType(ArrayLit &node) {
    for (ExprPtr &slot : node.elements()) {
        if (dynamic_cast<Blank *>(slot.get())) continue;
        if (ArrayLit *nested = dynamic_cast<ArrayLit *>(slot.get())) {
            const Type *inner = literalType(*nested);
            return inner ? Type::arrayOf(inner) : nullptr;
        }
        const Type *scalar = typeOf(slot);
        return scalar ? Type::arrayOf(scalar) : nullptr;
    }
    return nullptr;
}

void Checker::coerceLiteral(ArrayLit &node, const Type *arrayType) {
    node.setType(arrayType);
    if (!arrayType || !arrayType->isArray()) return;
    const Type *element = arrayType->element();
    for (ExprPtr &slot : node.elements()) {
        if (dynamic_cast<Blank *>(slot.get())) continue;
        if (ArrayLit *nested = dynamic_cast<ArrayLit *>(slot.get())) {
            coerceLiteral(*nested, element);
            continue;
        }
        typeOf(slot);
        coerce(slot, element);
    }
}

void Checker::visit(ArrayLit &node) {
    coerceLiteral(node, literalType(node));
}

void Checker::visit(Var &node) {
    const Symbol *symbol = lookup(node.name());
    if (!symbol) {
        if (isConstant(node.name())) {
            node.resolveConstant(constantValue(node.name()));
            node.setType(Type::realType());
            return;
        }
        reportUndefined(node.name());
        return;
    }
    node.resolve(symbol);
    node.setType(symbol->type());
}

void Checker::visit(Index &node) {
    const Type *base = typeOf(node.baseRef());
    const Type *index = typeOf(node.indexRef());
    if (!base) return;
    if (!base->isArray()) {
        diag_.error(unit_, line_, base->spelling() + " cannot be indexed");
        return;
    }
    if (index && index != Type::intType()) {
        diag_.error(unit_, line_, "An index must be int, not " + index->spelling());
    }
    node.setType(base->element());
}

void Checker::visit(Dim &node) {
    const Type *base = typeOf(node.base());
    const Type *axis = typeOf(node.axis());

    if (base && !base->isArray()) {
        diag_.error(unit_, line_, "'." + node.spelling() + "' needs an array, not " +
                               base->spelling());
    }
    if (axis && axis != Type::intType()) {
        diag_.error(unit_, line_, "Axis must be int, not " + axis->spelling());
    }
    node.setType(Type::intType());
}

void Checker::visit(Precision &node) {
    const Type *places = typeOf(node.places());
    if (places && places != Type::intType()) {
        diag_.error(unit_, line_, "prec() needs an int, not " + places->spelling());
    }
    node.setType(Type::intType());
}

void Checker::visit(Convert &node) {
    const Type *from = typeOf(node.operand());
    if (from && from->isArray()) {
        diag_.error(unit_, line_, "Cannot convert an array");
    }
}

void Checker::visit(Binary &node) {
    const Type *left = typeOf(node.left());
    const Type *right = typeOf(node.right());
    if (!left || !right) return;

    if (isText(left) && isText(right)) {
        switch (node.op()) {
        case Binary::Op::Equal:
        case Binary::Op::NotEqual:
        case Binary::Op::Less:
        case Binary::Op::Greater:
        case Binary::Op::LessEqual:
        case Binary::Op::GreaterEqual:
            node.setType(Type::intType());
            return;
        case Binary::Op::Add:
            node.setType(charArray());
            return;
        default:
            diag_.error(unit_, line_, std::string("'") + Binary::spelling(node.op()) +
                                   "' does not apply to strings");
            node.setType(Type::intType());
            return;
        }
    }

    if (left->isArray() || right->isArray()) {
        diag_.error(unit_, line_, std::string("'") + Binary::spelling(node.op()) +
                               "' needs scalars, got " + left->spelling() +
                               " and " + right->spelling());

        coerce(node.left(), common(left, right));
        coerce(node.right(), common(left, right));
        node.setType(Type::intType());
        return;
    }

    switch (node.op()) {
    case Binary::Op::Add:
    case Binary::Op::Subtract:
    case Binary::Op::Multiply:
    case Binary::Op::Divide:
    case Binary::Op::Modulus:
    case Binary::Op::Power:
        if (left == Type::charType() || right == Type::charType()) {
            diag_.error(unit_, line_, std::string("'") + Binary::spelling(node.op()) +
                                   "' does not apply to char");
            coerce(node.left(), common(left, right));
            coerce(node.right(), common(left, right));
            node.setType(Type::intType());
            return;
        }
        break;
    default:
        break;
    }

    const Type *operands = common(left, right);
    coerce(node.left(), operands);
    coerce(node.right(), operands);
    node.setType(Binary::yieldsInt(node.op()) ? Type::intType() : operands);
}

void Checker::visit(Call &node) {
    // The program's own function wins. A builtin is what the name means when
    // nothing in the file has claimed it, which is the rule C gets from
    // headers - sin is <math.h>'s until you declare your own - said here
    // without needing headers to say it.
    Function *user = program_->find(node.callee());
    // **And only if this file borrowed it.** A library function is not
    // available by being known; it is available by being asked for. Without
    // the `uses` the name falls through to the ordinary search for a function
    // in the project's other files, and is reported missing like any other.
    const int which = (user != nullptr || !program_->borrows(node.callee()))
                          ? -1
                          : findBuiltin(node.callee());
    if (which >= 0) {
        const Builtin &fn = builtin(which);
        node.resolveBuiltin(which);
        if (static_cast<int>(node.arguments().size()) != fn.arity) {
            diag_.error(unit_, line_, "'" + node.callee() + "' takes " + std::to_string(fn.arity) +
                                   ", got " + std::to_string(node.arguments().size()));
        }
        std::vector<const Type *> given;
        for (size_t i = 0; i < node.arguments().size(); ++i) {
            given.push_back(typeOf(node.arguments()[i]));
        }

        if (fn.shape == Builtin::Shape::Length) {
            if (!given.empty() && given[0] && !given[0]->isArray()) {
                diag_.error(unit_, line_, "len() needs an array, got " + given[0]->spelling());
            }
            node.setType(Type::intType());
            return;
        }
        bool allInt = true;
        for (const Type *type : given) {
            if (type != Type::intType()) allInt = false;
        }
        const bool intAnswer = fn.shape == Builtin::Shape::IntOrReal && allInt;
        const Type *want = intAnswer ? Type::intType() : Type::realType();
        for (ExprPtr &argument : node.arguments()) coerce(argument, want);
        node.setType(want);
        return;
    }

    Function *callee = user;
    const Prototype *declared = nullptr;
    if (!callee) {
        // A `uses` declaration, which carries its own prototype. It is checked
        // exactly as a written function's is - the declaration IS the contract,
        // and it is the only thing this compiler will ever know about the
        // callee.
        declared = program_->foreignNamed(node.callee());
    }
    if (!callee && declared == nullptr) {
        diag_.error(unit_, line_, "Unknown function '" + node.callee() + "'");
        for (ExprPtr &argument : node.arguments()) typeOf(argument);
        return;
    }
    if (callee) callee->markCalled();
    const Prototype &proto = callee ? callee->proto() : *declared;
    node.resolve(&proto);

    if (node.arguments().size() != proto.inputs.size()) {
        diag_.error(unit_, line_, "'" + node.callee() + "' takes " +
                               std::to_string(proto.inputs.size()) + ", got " +
                               std::to_string(node.arguments().size()));
    }

    for (size_t i = 0; i < node.arguments().size(); ++i) {
        const Type *given = typeOf(node.arguments()[i]);
        if (i >= proto.inputs.size() || !given) continue;
        const Param &parameter = proto.inputs[i];

        if (parameter.byReference || parameter.type->isArray()) {

            if (!node.arguments()[i]->isAddressable() && !parameter.type->isArray()) {
                diag_.error(unit_, line_, "Argument " + std::to_string(i + 1) + " of '" +
                                       node.callee() + "' needs a variable");
            } else if (given != parameter.type) {
                diag_.error(unit_, line_, "Argument " + std::to_string(i + 1) + " of '" +
                                       node.callee() + "' must be " +
                                       parameter.type->spelling());
            }
            continue;
        }
        coerce(node.arguments()[i], parameter.type);
    }

    int scratch = 0;
    if (proto.returnsByPointer()) scratch += static_cast<int>(proto.outputs.size());
    for (const Param &parameter : proto.inputs) {
        if (parameter.byReference) ++scratch;
    }
    if (scratch > 0) {
        const int base = function_->addHiddenSlot();
        for (int i = 1; i < scratch; ++i) function_->addHiddenSlot();
        node.setScratchBase(base);
    }

    node.setType(proto.outputs.empty() ? nullptr : proto.outputs[0]);
}

void Checker::visit(Declare &node) {
    refuseBorrowed(node.name(), line_);

    // `lookup` rather than `definedHere` for a local: a declaration may sit inside a
    // block now, and a declared local lives for the whole call, so one inside an `if`
    // may not shadow one outside it. `declaredLocals_` answers the other half - two
    // SIBLING blocks each declaring 't', whose scopes never exist at the same moment
    // for `scope_` to compare.
    const bool taken =
        inGlobalScope_ ? (scope_.definedHere(node.name()) || globals_.count(node.name()) != 0)
                       : (scope_.lookup(node.name()) != 0 ||
                          declaredLocals_.count(node.name()) != 0);
    if (taken) {
        diag_.error(unit_, line_, "Variable '" + node.name() + "' already defined");
        // Reported, and then this carries on and declares the name anyway. Returning
        // here left the name undefined, so every later mention of it drew a second
        // "Undefined variable" - one mistake, two messages, and the reader is sent
        // looking at the wrong line. The app's interpreter reports the redeclaration
        // alone, and the differential suite caught the difference the moment a case
        // used the name after declaring it twice.
    }

    const Type *type = node.declaredType();
    for (size_t i = 0; i < node.extents().size(); ++i) type = Type::arrayOf(type);
    if (!type->isWellFormed()) {
        diag_.error(unit_, line_, "'" + node.name() + "': " + type->spelling() +
                               " - strings are 1-D");
        type = charArray();
    }
    node.setDeclaredType(type);

    for (ExprPtr &extent : node.extents()) {
        const Type *given = typeOf(extent);
        if (given && given != Type::intType()) {
            diag_.error(unit_, line_, "'" + node.name() + "': size must be int, not " +
                                   given->spelling());
        }

        double folded = 0.0;
        if (constantNumber(*extent, folded) && folded < 1) {
            diag_.error(unit_, line_, "'" + node.name() + "': size must be 1 or more, got " +
                                   number(folded));
        }
    }
    if (!node.extents().empty()) {
        const int base = function_->addHiddenSlot();
        for (size_t i = 1; i < node.extents().size(); ++i) function_->addHiddenSlot();
        node.setExtentBase(base);
    }

    if (node.initial()) {
        if (ArrayLit *literal = dynamic_cast<ArrayLit *>(node.initial().get())) {
            coerceLiteral(*literal, type);
        } else {
            const Type *given = typeOf(node.initial());
            if (given && given->isArray() && given != type) {
                diag_.error(unit_, line_, "'" + node.name() + "' is " + type->spelling());
            }
            coerce(node.initial(), type);
        }
    }

    Symbol *symbol = declareName(node.name(), type);
    if (!inGlobalScope_) {
        // Into the innermost level, which is what ends the name's VISIBILITY with its
        // block - the rule a name made by a first assignment already follows. The
        // lifetime is a separate question and unchanged: the slot is the frame's.
        scope_.define(node.name(), symbol);
        declaredLocals_.insert(node.name());

        if (globals_.count(node.name())) {
            diag_.warning(unit_, line_, "'" + node.name() + "' hides a global");
        }
    }
    node.resolve(symbol);
}

void Checker::visit(Assign &node) {
    Index *element = dynamic_cast<Index *>(node.target().get());
    if (element) {
        const Type *target = typeOf(node.target());
        if (!target) return;

        if (target->isArray()) {
            if (ArrayLit *literal = dynamic_cast<ArrayLit *>(node.expr().get())) {
                coerceLiteral(*literal, target);
                return;
            }

            const Type *value = typeOf(node.expr());
            if (!value) return;
            if (!value->isArray() || value->rank() != target->rank() ||
                value->scalar() != target->scalar()) {
                diag_.error(unit_, line_,
                            "Cannot use " + value->spelling() + " where " +
                                target->spelling() + " is required");
                return;
            }
            return;
        }

        const Type *value = typeOf(node.expr());
        if (!value) return;
        coerce(node.expr(), target);
        return;
    }

    Var &target = static_cast<Var &>(*node.target());
    if (refuseConstant(target.name(), "assigned")) return;
    refuseBorrowed(target.name(), line_);

    const Symbol *existing = lookup(target.name());

    ArrayLit *literal = dynamic_cast<ArrayLit *>(node.expr().get());
    const Type *value = nullptr;
    if (literal && existing && existing->type()->isArray()) {
        coerceLiteral(*literal, existing->type());
        value = existing->type();
    } else if (literal) {
        value = literalType(*literal);
        if (!value) {
            diag_.error(unit_, line_, "An all-blank literal cannot create '" + target.name() + "'");
            return;
        }
        coerceLiteral(*literal, value);
    } else {
        value = typeOf(node.expr());
    }
    if (!value) return;

    if (!existing) {

        if (value->isArray() && !literal && !isText(value)) {
            diag_.error(unit_, line_, "Declare the array '" + target.name() + "' first");
            return;
        }
        Symbol *created = declareName(target.name(), value);
        if (!inGlobalScope_) scope_.define(target.name(), created);
        existing = created;
        node.setCreates(true);
    }
    target.resolve(existing);
    target.setType(existing->type());
    if (!existing->type()->isArray()) coerce(node.expr(), existing->type());
    else if (value != existing->type()) {
        diag_.error(unit_, line_, "'" + target.name() + "' is " + existing->type()->spelling());
    }
    node.resolve(existing);
}

void Checker::visit(CompoundAssign &node) {
    const Type *target = typeOf(node.target());
    const Type *value = typeOf(node.expr());
    if (!target || !value) return;

    if (isText(target)) {
        if (!node.isAdd()) {
            diag_.error(unit_, line_, "'-:' does not apply to strings");
            return;
        }

        coerce(node.expr(), target);
        return;
    }
    if (target->isArray()) {
        diag_.error(unit_, line_, std::string("'") + (node.isAdd() ? "+:" : "-:") +
                               "' needs a single value");
        return;
    }
    coerce(node.expr(), target);
}

void Checker::visit(MultiAssign &node) {
    typeOf(node.call());

    Call &call = static_cast<Call &>(*node.call());

    std::vector<const Type *> outputs;
    if (call.builtin() >= 0) outputs.push_back(call.type());
    else if (call.prototype()) outputs = call.prototype()->outputs;
    else return;

    if (node.names().size() != outputs.size()) {
        diag_.error(unit_, line_, "'" + call.callee() + "' returns " +
                               std::to_string(outputs.size()) + ", not " +
                               std::to_string(node.names().size()));
        return;
    }
    for (size_t i = 0; i < node.names().size(); ++i) {
        if (refuseConstant(node.names()[i], "assigned")) return;
        refuseBorrowed(node.names()[i], line_);
        const Symbol *target = lookup(node.names()[i]);
        if (!target) {
            Symbol *created = declareName(node.names()[i], outputs[i]);
            if (!inGlobalScope_) scope_.define(node.names()[i], created);
            target = created;
        } else if (target->type() != outputs[i]) {

            const bool convertible = !target->type()->isArray() && !outputs[i]->isArray();
            if (!convertible) {
                diag_.error(unit_, line_, "'" + node.names()[i] + "' is " +
                                       target->type()->spelling() + ", and '" + call.callee() +
                                       "' gives " + outputs[i]->spelling() +
                                       " - which do not convert");
                return;
            }
        }
        node.targets().push_back(target);
    }
}

void Checker::visit(CallStmt &node) { typeOf(node.call()); }

void Checker::visit(Return &node) {
    const size_t declared = function_ ? function_->proto().outputs.size() : 0;
    if (node.exprs().size() != declared) {
        diag_.error(unit_, line_, "'" + function_->proto().name + "' returns " +
                               std::to_string(declared) + " values, not " +
                               std::to_string(node.exprs().size()));
    }
    for (size_t i = 0; i < node.exprs().size(); ++i) {
        typeOf(node.exprs()[i]);
        if (i < declared) coerce(node.exprs()[i], function_->proto().outputs[i]);
    }
}

void Checker::visit(Print &node) {
    for (ExprPtr &item : node.items()) typeOf(item);
}

void Checker::checkCondition(ExprPtr &expr) {
    const Type *type = typeOf(expr);
    if (type && type->isArray()) {
        diag_.error(unit_, line_, "Condition cannot be " + type->spelling());
    }
}

void Checker::checkBlock(Block &body) {
    scope_.push();
    for (StmtPtr &s : body) check(*s);
    scope_.pop();
}

void Checker::visit(If &node) {
    for (If::Branch &branch : node.branches()) {
        checkCondition(branch.condition);
        checkBlock(branch.body);
    }
    if (node.hasElse()) checkBlock(node.elseBody());
}

void Checker::visit(While &node) {
    checkCondition(node.condition());
    checkBlock(node.body());
}

void Checker::visit(For &node) {
    const Type *counterType = common(typeOf(node.start()), typeOf(node.end()));
    if (node.step()) counterType = common(counterType, typeOf(node.step()));
    if (!counterType || counterType->isArray()) {
        if (counterType) diag_.error(unit_, line_, "Loop counter cannot be " + counterType->spelling());
        counterType = Type::intType();
    }

    coerce(node.start(), counterType);
    coerce(node.end(), counterType);
    if (node.step()) coerce(node.step(), counterType);

    warnIfLoopNeverRuns(node);
    refuseConstant(node.variable(), "used as a loop counter");
    refuseBorrowed(node.variable(), line_);

    const int base = function_->addHiddenSlot();
    for (int i = 1; i < For::HiddenSlotCount; ++i) function_->addHiddenSlot();
    node.setHiddenBase(base);

    scope_.push();
    Symbol *counter = function_->declare(node.variable(), counterType);
    scope_.define(node.variable(), counter);
    node.resolve(counter);
    checkBlock(node.body());
    scope_.pop();
}

void Checker::visit(Break &) {}
void Checker::visit(Continue &) {}

bool Checker::constantNumber(const Expr &expr, double &value) const {
    if (expr.isIntLiteral())  { value = static_cast<const IntLit &>(expr).value();  return true; }
    if (expr.isRealLiteral()) { value = static_cast<const RealLit &>(expr).value(); return true; }
    if (const Convert *conversion = dynamic_cast<const Convert *>(&expr)) {
        return constantNumber(conversion->expr(), value);
    }
    const Binary *binary = dynamic_cast<const Binary *>(&expr);
    if (!binary) return false;
    double left = 0.0;
    double right = 0.0;
    if (!constantNumber(binary->lhs(), left)) return false;
    if (!constantNumber(binary->rhs(), right)) return false;
    switch (binary->op()) {
    case Binary::Op::Add:      value = left + right; return true;
    case Binary::Op::Subtract: value = left - right; return true;
    case Binary::Op::Multiply: value = left * right; return true;
    default: return false;
    }
}

std::string Checker::number(double value) {
    if (value == static_cast<double>(static_cast<long long>(value)) &&
        value >= -9.2e18 && value <= 9.2e18) {
        return std::to_string(static_cast<long long>(value));
    }
    return std::to_string(value);
}

void Checker::warnIfLoopNeverRuns(For &node) {
    double start = 0.0;
    double end = 0.0;
    double step = 1.0;
    if (!constantNumber(*node.start(), start)) return;
    if (!constantNumber(*node.end(), end)) return;
    if (node.step() && !constantNumber(*node.step(), step)) return;
    if (step == 0) return;
    if (step > 0 ? start <= end : start >= end) return;

    diag_.warning(unit_, line_, "Loop never runs: '" + node.variable() + "' starts at " +
                             number(start) + " and step " + number(step) +
                             " moves away from " + number(end));
}

}
