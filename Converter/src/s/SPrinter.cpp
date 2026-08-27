#include "SPrinter.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "SBeyond.h"

namespace c2s {

SPrinter::SPrinter() : depth_(0), floor_(TierOr) {}

SPrinter::Tier SPrinter::tierOf(shalimar::Binary::Op op) {
    using Op = shalimar::Binary::Op;
    switch (op) {
        case Op::Or:           return TierOr;
        case Op::And:          return TierAnd;
        case Op::Equal:
        case Op::NotEqual:
        case Op::Less:
        case Op::Greater:
        case Op::LessEqual:
        case Op::GreaterEqual: return TierComparison;
        case Op::Add:
        case Op::Subtract:     return TierAdditive;
        case Op::Multiply:
        case Op::Divide:
        case Op::Modulus:      return TierMultiplicative;
        case Op::Power:        return TierPower;
    }
    return TierPrimary;
}

std::string SPrinter::spellReal(double value) {

    char buffer[64];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*g", precision, value);
        char *end = nullptr;
        if (std::strtod(buffer, &end) == value && end == buffer + std::strlen(buffer)) break;
    }

    std::string text = buffer;
    if (text.find('.') == std::string::npos &&
        text.find('e') == std::string::npos &&
        text.find('E') == std::string::npos) {
        text += ".0";
    }
    return text;
}

void SPrinter::indent() {
    for (int i = 0; i < depth_; ++i) out_ += "  ";
}

void SPrinter::line(const std::string &text) {
    indent();
    out_ += text;
    out_ += '\n';
}

void SPrinter::expr(shalimar::Expr &node, int floor) {
    const int saved = floor_;
    floor_ = floor;
    node.accept(*this);
    floor_ = saved;
}

void SPrinter::visit(shalimar::IntLit &node) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%d", static_cast<int>(node.value()));
    out_ += buffer;
}

void SPrinter::visit(shalimar::RealLit &node) {
    out_ += spellReal(node.value());
}

void SPrinter::visit(shalimar::Var &node) {
    out_ += node.name();
}

void SPrinter::visit(shalimar::Convert &node) {

    const shalimar::Type *to = node.type();
    out_ += to != nullptr ? to->spelling() : "int";
    out_ += '(';
    expr(node.expr(), TierOr);
    out_ += ')';
}

void SPrinter::visit(shalimar::Binary &node) {
    const Tier tier = tierOf(node.op());
    const bool parens = tier < floor_;
    if (parens) out_ += '(';

    const bool rightAssociative = node.op() == shalimar::Binary::Op::Power;
    const int leftFloor = rightAssociative ? tier + 1 : tier;
    const int rightFloor = rightAssociative ? tier : tier + 1;

    expr(node.lhs(), leftFloor);
    out_ += ' ';
    out_ += shalimar::Binary::spelling(node.op());
    out_ += ' ';
    expr(node.rhs(), rightFloor);

    if (parens) out_ += ')';
}

void SPrinter::visit(shalimar::StrLit &node) {

    out_ += '"';
    out_ += node.text();
    out_ += '"';
}

void SPrinter::visit(shalimar::ArrayLit &node) {
    out_ += '{';
    std::vector<shalimar::ExprPtr> &elements = node.elements();
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) out_ += ", ";
        expr(*elements[i], TierOr);
    }
    out_ += '}';
}

void SPrinter::visit(shalimar::Blank &) {

}

void SPrinter::visit(shalimar::Index &node) {
    expr(node.base(), TierPrimary);
    out_ += '[';
    expr(node.index(), TierOr);
    out_ += ']';
}

void SPrinter::visit(shalimar::Dim &node) {
    expr(*node.base(), TierPrimary);
    out_ += '.';
    if (node.spelling() == "dim") {
        out_ += "dim(";
        expr(*node.axis(), TierOr);
        out_ += ')';
    } else {
        out_ += node.spelling();
    }
}

void SPrinter::visit(shalimar::Precision &node) {
    out_ += "prec(";
    expr(*node.places(), TierOr);
    out_ += ')';
}

void SPrinter::visit(shalimar::Call &node) {
    out_ += node.callee();
    out_ += '(';
    std::vector<shalimar::ExprPtr> &arguments = node.arguments();
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        if (i > 0) out_ += ", ";
        expr(*arguments[i], TierOr);
    }
    out_ += ')';
}

void SPrinter::declare(shalimar::Declare &node) {

    const shalimar::Type *type = node.declaredType();
    std::string text = type->scalar()->spelling();
    text += ' ';
    text += node.name();

    indent();
    out_ += text;
    std::vector<shalimar::ExprPtr> &extents = node.extents();
    for (std::size_t i = 0; i < extents.size(); ++i) {
        out_ += '[';
        expr(*extents[i], TierOr);
        out_ += ']';
    }
    if (node.initial() != nullptr) {
        out_ += " : ";
        expr(*node.initial(), TierOr);
    }
    out_ += '\n';
}

void SPrinter::visit(shalimar::Declare &node) {
    declare(node);
}

void SPrinter::visit(shalimar::Assign &node) {
    indent();
    expr(*node.target(), TierPrimary);
    out_ += " : ";
    expr(*node.expr(), TierOr);
    out_ += '\n';
}

void SPrinter::visit(shalimar::CompoundAssign &node) {
    indent();
    expr(*node.target(), TierPrimary);
    out_ += node.isAdd() ? " +: " : " -: ";
    expr(*node.expr(), TierOr);
    out_ += '\n';
}

void SPrinter::visit(shalimar::Print &node) {

    indent();
    out_ += node.newline() ? "?" : "?\?";
    std::vector<shalimar::ExprPtr> &items = node.items();
    for (std::size_t i = 0; i < items.size(); ++i) {
        out_ += ' ';

        std::string kept;
        kept.swap(out_);
        expr(*items[i], TierOr);
        std::string item;
        item.swap(out_);
        out_.swap(kept);

        std::size_t j = 0;
        if (j < item.size() && (std::isalpha(static_cast<unsigned char>(item[j])) != 0 ||
                                item[j] == '_')) {
            while (j < item.size() && (std::isalnum(static_cast<unsigned char>(item[j])) != 0 ||
                                       item[j] == '_')) ++j;
            while (j < item.size() && item[j] == ' ') ++j;
            if (j < item.size() && item[j] == '=' &&
                (j + 1 >= item.size() || item[j + 1] != '=')) {
                item = "(" + item + ")";
            }
        }
        out_ += item;
    }
    out_ += '\n';
}

void SPrinter::visit(shalimar::Return &node) {
    indent();
    out_ += "return";
    std::vector<shalimar::ExprPtr> &exprs = node.exprs();
    if (exprs.size() == 1) {
        out_ += ' ';
        expr(*exprs[0], TierOr);
    } else if (exprs.size() > 1) {
        out_ += " (";
        for (std::size_t i = 0; i < exprs.size(); ++i) {
            if (i > 0) out_ += ", ";
            expr(*exprs[i], TierOr);
        }
        out_ += ')';
    }
    out_ += '\n';
}

void SPrinter::visit(shalimar::MultiAssign &node) {
    indent();
    out_ += '<';
    const std::vector<std::string> &names = node.names();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out_ += ", ";
        out_ += names[i];
    }
    out_ += "> : ";
    expr(*node.call(), TierPrimary);
    out_ += '\n';
}

void SPrinter::visit(shalimar::CallStmt &node) {
    indent();
    expr(*node.call(), TierPrimary);
    out_ += '\n';
}

void SPrinter::visit(shalimar::If &node) {
    std::vector<shalimar::If::Branch> &branches = node.branches();
    for (std::size_t i = 0; i < branches.size(); ++i) {
        indent();
        // **`else if`, not `elseif`.** Both are the same branch to Shalimar,
        // and this one makes the converted chain the picture of the C it came
        // from - same words, same order, same layout, with only the condition
        // losing its parentheses. Reading a conversion against its source is
        // most of what this tool is for.
        //
        // There is no alternative spelling to fall back on: Shalimar dropped
        // the one-word `elseif` on 2026-08-26 and did not keep it reserved, so
        // this is simply what a branch is called. Output from here needs an
        // shc of that date or later.
        out_ += i == 0 ? "if " : "else if ";
        expr(*branches[i].condition, TierOr);
        out_ += " {\n";
        block(branches[i].body);
        indent();
        out_ += "}\n";
    }
    if (node.hasElse()) {
        indent();
        out_ += "else {\n";
        block(node.elseBody());
        indent();
        out_ += "}\n";
    }
}

void SPrinter::visit(shalimar::While &node) {
    indent();
    out_ += "while ";
    expr(*node.condition(), TierOr);
    out_ += " {\n";
    block(node.body());
    indent();
    out_ += "}\n";
}

void SPrinter::visit(shalimar::For &node) {

    indent();
    out_ += "for ";
    out_ += node.variable();
    out_ += " : ";
    expr(*node.start(), TierOr);
    out_ += " to ";
    expr(*node.end(), TierOr);
    if (node.step() != nullptr) {
        out_ += " step ";
        expr(*node.step(), TierOr);
    }
    out_ += " {\n";
    block(node.body());
    indent();
    out_ += "}\n";
}

void SPrinter::visit(shalimar::Break &) {
    line("break");
}

void SPrinter::visit(shalimar::Continue &) {
    line("continue");
}

void SPrinter::statement(shalimar::Stmt &node) {

    if (SBeyondStmt *marker = dynamic_cast<SBeyondStmt *>(&node)) {

        line("// #BEYOND SHALIMAR: " + marker->reason());
        const std::vector<std::string> &lines = marker->lines();
        for (std::size_t i = 0; i < lines.size(); ++i) {
            line("//    " + lines[i]);
        }
        return;
    }
    node.accept(*this);
}

void SPrinter::block(shalimar::Block &body) {
    ++depth_;
    for (std::size_t i = 0; i < body.size(); ++i) statement(*body[i]);
    --depth_;
}

// A function's body, which is a block plus one piece of punctuation: **a blank
// line between the declarations and the code**. Shalimar gathers every
// declaration at the top of a function, so that run can be long, and without
// the gap the first real statement is just the next line down. Only the
// leading run counts - a `Declare` further in belongs to whatever it sits
// among and gets no line of its own.
void SPrinter::functionBody(shalimar::Block &body) {
    std::size_t declarations = 0;
    while (declarations < body.size() &&
           dynamic_cast<shalimar::Declare *>(body[declarations].get()) != nullptr) {
        ++declarations;
    }

    ++depth_;
    for (std::size_t i = 0; i < body.size(); ++i) {
        // Not when the whole body is declarations, and not when there are
        // none: a blank line at the end of a function, or at the start of one,
        // is a gap around nothing.
        if (i == declarations && declarations > 0) out_ += '\n';
        statement(*body[i]);
    }
    --depth_;
}

void SPrinter::functionHeader(const shalimar::Prototype &proto) {
    indent();
    out_ += "fun <";
    for (std::size_t i = 0; i < proto.outputs.size(); ++i) {
        if (i > 0) out_ += ", ";
        out_ += proto.outputs[i]->spelling();
    }
    out_ += "> = ";
    out_ += proto.name;
    out_ += '(';
    for (std::size_t i = 0; i < proto.inputs.size(); ++i) {
        if (i > 0) out_ += ", ";
        const shalimar::Param &param = proto.inputs[i];

        const shalimar::Type *type = param.type;
        const int rank = type != nullptr ? type->rank() : 0;
        if (param.byReference && rank == 0) out_ += '&';
        out_ += param.name;
        for (int r = 0; r < rank; ++r) out_ += "[]";
        out_ += ": ";
        out_ += type != nullptr ? type->scalar()->spelling() : "int";
    }
    // **The function's `{` goes on its own line**, while `if`, `while` and
    // `for` keep theirs on the same line as the condition. That mix is not an
    // oversight - it is how the language is written by hand: the head of a
    // function is a thing you read on its own, and a control-flow brace reads
    // as part of the line that opens it.
    out_ += ")\n";
    indent();
    out_ += "{\n";
}

std::string SPrinter::print(shalimar::Program &program) {
    out_.clear();
    depth_ = 0;

    // What the converted program borrows, one clause at the top. Shalimar has
    // no library function until a file asks for it - see
    // ../Compiler-S/docs/FOREIGN.md - so output that calls `sqrt` and does not
    // say so does not compile.
    //
    // De-duplicated in order of first use, which keeps the line stable: a
    // program calling sin twice and cos once reads `uses sin, cos` however the
    // calls are arranged in the file.
    std::vector<std::string> borrowed;
    for (std::size_t i = 0; i < program.borrowed().size(); ++i) {
        const std::string &name = program.borrowed()[i].name;
        bool seen = false;
        for (std::size_t j = 0; j < borrowed.size(); ++j)
            if (borrowed[j] == name) { seen = true; break; }
        if (!seen) borrowed.push_back(name);
    }
    if (!borrowed.empty()) {
        out_ += "uses ";
        for (std::size_t i = 0; i < borrowed.size(); ++i) {
            if (i > 0) out_ += ", ";
            out_ += borrowed[i];
        }
        out_ += "\n\n";
    }

    const std::vector<shalimar::Program::Entry> &order = program.order();
    bool first = true;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const shalimar::Program::Entry &entry = order[i];
        if (entry.isFunction) {
            shalimar::Function &fn = *program.functions()[entry.index];
            if (fn.isRejected()) continue;
            if (!first) out_ += '\n';
            functionHeader(fn.proto());
            functionBody(fn.body());
            out_ += "}\n";
        } else {
            shalimar::Stmt &global = *program.globals()[entry.index];
            statement(global);
        }
        first = false;
    }
    return out_;
}

std::string SPrinter::printExpr(shalimar::Expr &node) {
    std::string kept;
    kept.swap(out_);
    expr(node, TierOr);
    std::string result;
    result.swap(out_);
    out_.swap(kept);
    return result;
}

}
