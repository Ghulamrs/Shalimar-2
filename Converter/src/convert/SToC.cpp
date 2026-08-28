#include "SToC.h"

#include "../Diagnostics.h"
#include "../Source.h"
#include "../s/SPrinter.h"
#include "../s/vendor/Builtin.h"

namespace c2s {

namespace {

bool isCKeyword(const std::string &word) {
    static const char *const keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "int", "long", "register", "return", "short", "signed", "sizeof",
        "static", "struct", "switch", "typedef", "union", "unsigned", "void",
        "volatile", "while", "main"
    };
    for (std::size_t i = 0; i < sizeof keywords / sizeof keywords[0]; ++i) {
        if (word == keywords[i]) return true;
    }
    return false;
}

CExprPtr ident(const std::string &name) {
    return CExprPtr(new CIdent(name));
}

CExprPtr intLit(long long value) {
    char text[24];
    std::snprintf(text, sizeof text, "%lld", value);
    return CExprPtr(new CIntLit(value, text));
}

CTypePtr basic(CType::Kind kind) {
    return CTypePtr(new CType(kind));
}

CTypePtr pointerTo(CTypePtr base) {
    CTypePtr type(new CType(CType::Kind::Pointer));
    type->setBase(std::move(base));
    return type;
}

void shapeOfLiteral(shalimar::ArrayLit &lit, std::vector<long long> *dims) {
    dims->push_back(static_cast<long long>(lit.elements().size()));
    if (!lit.elements().empty()) {
        if (shalimar::ArrayLit *inner =
                dynamic_cast<shalimar::ArrayLit *>(lit.elements()[0].get())) {
            shapeOfLiteral(*inner, dims);
        }
    }
}

void flattenLiteral(shalimar::ArrayLit &lit, std::vector<shalimar::Expr *> *out) {
    std::vector<shalimar::ExprPtr> &elements = lit.elements();
    for (std::size_t i = 0; i < elements.size(); ++i) {
        if (shalimar::ArrayLit *inner =
                dynamic_cast<shalimar::ArrayLit *>(elements[i].get())) {
            flattenLiteral(*inner, out);
        } else {
            out->push_back(elements[i].get());
        }
    }
}

}

SToC::SToC(const Source &source, Diagnostics &diagnostics)
    : source_(source), diagnostics_(diagnostics) {}

std::string SToC::sanitise(const std::string &name) {
    if (isCKeyword(name) || name.compare(0, 4, "c2s_") == 0) return name + "_v";
    return name;
}

std::string SToC::freshName(const std::string &base) {
    char text[32];
    std::snprintf(text, sizeof text, "c2s_%s%d", base.c_str(), ++tempCount_);
    return text;
}

const SToC::Info *SToC::infoFor(const shalimar::Symbol *symbol) const {
    std::map<const shalimar::Symbol *, Info>::const_iterator it = symbols_.find(symbol);
    return it == symbols_.end() ? nullptr : &it->second;
}

const SToC::Info *SToC::lookupVar(const shalimar::Var &var) {
    if (var.symbol() != nullptr) {
        std::map<const shalimar::Symbol *, Info>::iterator it =
            symbols_.find(var.symbol());
        if (it != symbols_.end()) return &it->second;
        std::map<std::string, Info>::const_iterator pit = paramInfos_.find(var.name());
        if (pit != paramInfos_.end()) {
            symbols_[var.symbol()] = pit->second;
            return &symbols_[var.symbol()];
        }
        return nullptr;
    }
    std::map<std::string, Info>::const_iterator pit = paramInfos_.find(var.name());
    return pit != paramInfos_.end() ? &pit->second : nullptr;
}

std::string SToC::cEscape(const std::string &text) {

    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\\' || c == '"') { out += '\\'; out += c; }
        else out += c;
    }
    return out;
}

void SToC::markBeyond(int line, const std::string &reason) {
    ++beyondCount_;

    // The same as CToS's, and for the same reason - see the note there. This
    // side carries a line rather than an offset, so the column is 1: the whole
    // statement is what has no C form, not one character of it.
    diagnostics_.report(Severity::ConversionError, source_,
                        Location(source_.name(), line, 1), "C2100", reason,
                        "there is no C form for this - the converted program "
                        "is marked where it stands, and will not compile");
    std::vector<std::string> lines;
    const std::string text = source_.line(line);
    if (!text.empty()) lines.push_back(text);
    CStmtPtr marker(new CBeyond(reason, lines));
    if (block_ != nullptr) block_->add(std::move(marker));
}

bool SToC::foldInt(shalimar::Expr &node, long long *out) const {
    if (shalimar::IntLit *lit = dynamic_cast<shalimar::IntLit *>(&node)) {
        *out = lit->value();
        return true;
    }
    if (shalimar::Binary *binary = dynamic_cast<shalimar::Binary *>(&node)) {
        long long left = 0;
        long long right = 0;
        if (!foldInt(binary->lhs(), &left) || !foldInt(binary->rhs(), &right)) return false;
        switch (binary->op()) {
            case shalimar::Binary::Op::Add:      *out = left + right; return true;
            case shalimar::Binary::Op::Subtract: *out = left - right; return true;
            case shalimar::Binary::Op::Multiply: *out = left * right; return true;
            case shalimar::Binary::Op::Divide:
                if (right == 0) return false;
                *out = left / right;
                return true;
            default: return false;
        }
    }
    return false;
}

CTypePtr SToC::scalarC(const shalimar::Type *type) const {
    if (type == nullptr) return basic(CType::Kind::Int);
    const shalimar::Type *scalar = type->scalar();
    switch (scalar->kind()) {
        case shalimar::Type::Kind::Real: return basic(CType::Kind::Double);
        case shalimar::Type::Kind::Char: return basic(CType::Kind::Char);
        default:                         return basic(CType::Kind::Int);
    }
}

void SToC::need(const std::string &helper) {
    helpers_[helper] = true;
    if (helper.compare(0, 9, "c2s_print") == 0 || helper == "c2s_line_end") {
        usesPrint_ = true;
    }
}

CExprPtr SToC::callHelper(const std::string &name, std::vector<CExprPtr> args) {
    need(name);
    std::unique_ptr<CCall> call(new CCall(ident(name)));
    for (std::size_t i = 0; i < args.size(); ++i) call->add(std::move(args[i]));
    return CExprPtr(call.release());
}

CExprPtr SToC::dimValue(const Info &info, int axis) {
    if (info.isParamArray) {
        char suffix[16];
        std::snprintf(suffix, sizeof suffix, "_d%d", axis);
        return ident(info.cName + suffix);
    }
    if (axis < static_cast<int>(info.extents.size())) {
        return intLit(info.extents[static_cast<std::size_t>(axis)]);
    }
    return intLit(0);
}

CExprPtr SToC::linearIndex(shalimar::Expr &chain, int *outRankLeft,
                           const Info **outInfo) {

    std::vector<shalimar::Index *> steps;
    shalimar::Expr *walk = &chain;
    while (shalimar::Index *step = dynamic_cast<shalimar::Index *>(walk)) {
        steps.push_back(step);
        walk = &step->base();
    }
    shalimar::Var *var = dynamic_cast<shalimar::Var *>(walk);
    if (var == nullptr) {
        *outInfo = nullptr;
        return nullptr;
    }
    const Info *info = lookupVar(*var);
    if (info == nullptr || info->rank == 0) {
        *outInfo = nullptr;
        return nullptr;
    }
    *outInfo = info;

    std::vector<CExprPtr> indices;
    for (std::size_t i = steps.size(); i > 0; --i) {
        indices.push_back(expression(steps[i - 1]->index()));
    }

    CExprPtr offset;
    for (std::size_t axis = 0; axis < indices.size(); ++axis) {
        if (offset == nullptr) {
            offset = std::move(indices[axis]);
        } else {
            CExprPtr scaled(new CBinary("*", std::move(offset),
                                        dimValue(*info, static_cast<int>(axis))));
            offset.reset(new CBinary("+", std::move(scaled), std::move(indices[axis])));
        }
    }

    const int consumed = static_cast<int>(steps.size());
    *outRankLeft = info->rank - consumed;

    if (*outRankLeft == 0) {
        CExprPtr element(new CIndex(ident(info->cName), std::move(offset)));
        return element;
    }

    CExprPtr stride;
    for (int axis = consumed; axis < info->rank; ++axis) {
        CExprPtr dim = dimValue(*info, axis);
        if (stride == nullptr) stride = std::move(dim);
        else stride.reset(new CBinary("*", std::move(stride), std::move(dim)));
    }
    CExprPtr start(new CBinary("*", std::move(offset), std::move(stride)));
    CExprPtr slice(new CBinary("+", ident(info->cName), std::move(start)));
    return slice;
}

CStmtPtr SToC::declStmtFor(const std::string &name, CTypePtr type, CExprPtr init) {
    std::unique_ptr<CDeclaration> decl(new CDeclaration());
    CDeclaration::Declarator declarator;
    declarator.name = name;
    declarator.type = std::move(type);
    if (init != nullptr) declarator.init.reset(new CInit(std::move(init)));
    decl->add(std::move(declarator));
    return CStmtPtr(new CDeclStmt(std::move(decl)));
}

CExprPtr SToC::expression(shalimar::Expr &node) {
    CExprPtr saved = std::move(expr_);
    expr_.reset();
    node.accept(*this);
    CExprPtr result = std::move(expr_);
    expr_ = std::move(saved);
    if (result == nullptr) result = intLit(0);
    return result;
}

void SToC::visit(shalimar::IntLit &node) {
    expr_ = intLit(node.value());
}

void SToC::visit(shalimar::RealLit &node) {
    const std::string spelling = SPrinter::spellReal(node.value());
    expr_.reset(new CFloatLit(node.value(), spelling));
}

void SToC::visit(shalimar::Var &node) {
    if (node.isNamedConstant()) {

        const std::string spelling = SPrinter::spellReal(node.constant());
        expr_.reset(new CFloatLit(node.constant(), spelling));
        return;
    }
    const Info *info = lookupVar(node);
    if (info == nullptr) {
        expr_ = ident(sanitise(node.name()));
        return;
    }
    if (info->isRefScalar) {
        expr_.reset(new CUnary("*", true, ident(info->cName)));
        return;
    }
    expr_ = ident(info->cName);
}

void SToC::visit(shalimar::Convert &node) {
    CExprPtr operand = expression(node.expr());
    const shalimar::Type *to = node.type();
    CTypePtr type = scalarC(to);
    expr_.reset(new CCast(std::move(type), std::move(operand)));
}

void SToC::visit(shalimar::Binary &node) {
    using Op = shalimar::Binary::Op;

    const shalimar::Type *operands = node.lhs().type();
    const bool realOperands =
        operands != nullptr && operands->kind() == shalimar::Type::Kind::Real;

    if (operands != nullptr && operands->isArray()) {
        markBeyond(0, std::string("the '") + shalimar::Binary::spelling(node.op()) +
                          "' operator on text has no C89 translation here yet");
        expr_.reset();
        return;
    }

    if (node.op() == Op::Power) {
        CExprPtr lhs = expression(node.lhs());
        CExprPtr rhs = expression(node.rhs());
        std::vector<CExprPtr> args;
        args.push_back(std::move(lhs));
        args.push_back(std::move(rhs));
        if (realOperands) {
            usesMath_ = true;
            std::unique_ptr<CCall> call(new CCall(ident("pow")));
            call->add(std::move(args[0]));
            call->add(std::move(args[1]));
            expr_.reset(call.release());
        } else {
            expr_ = callHelper("c2s_int_pow", std::move(args));
        }
        return;
    }

    if (node.op() == Op::Modulus && realOperands) {
        usesMath_ = true;
        std::unique_ptr<CCall> call(new CCall(ident("fmod")));
        call->add(expression(node.lhs()));
        call->add(expression(node.rhs()));
        expr_.reset(call.release());
        return;
    }

    if (node.op() == Op::And || node.op() == Op::Or) {

        CExprPtr lhs(new CBinary("!=", expression(node.lhs()), intLit(0)));
        CExprPtr rhs(new CBinary("!=", expression(node.rhs()), intLit(0)));
        expr_.reset(new CBinary(node.op() == Op::And ? "&" : "|",
                                std::move(lhs), std::move(rhs)));
        return;
    }

    const bool intOperands =
        operands != nullptr && operands->kind() == shalimar::Type::Kind::Int;
    if (intOperands && (node.op() == Op::Add || node.op() == Op::Subtract ||
                        node.op() == Op::Multiply)) {
        const char *helper = node.op() == Op::Add
                                 ? "c2s_add_int"
                                 : (node.op() == Op::Subtract ? "c2s_sub_int"
                                                              : "c2s_mul_int");
        std::vector<CExprPtr> args;
        args.push_back(expression(node.lhs()));
        args.push_back(expression(node.rhs()));
        args.push_back(intLit(currentLine_));
        expr_ = callHelper(helper, std::move(args));
        return;
    }

    const char *op = nullptr;
    switch (node.op()) {
        case Op::Add:          op = "+"; break;
        case Op::Subtract:     op = "-"; break;
        case Op::Multiply:     op = "*"; break;
        case Op::Divide:       op = "/"; break;
        case Op::Modulus:      op = "%"; break;
        case Op::Equal:        op = "=="; break;
        case Op::NotEqual:     op = "!="; break;
        case Op::Less:         op = "<"; break;
        case Op::Greater:      op = ">"; break;
        case Op::LessEqual:    op = "<="; break;
        case Op::GreaterEqual: op = ">="; break;
        default: break;
    }
    expr_.reset(new CBinary(op != nullptr ? op : "+",
                            expression(node.lhs()), expression(node.rhs())));
}

void SToC::visit(shalimar::StrLit &node) {

    expr_.reset(new CStringLit(node.text(), "\"" + cEscape(node.text()) + "\""));
}

void SToC::visit(shalimar::ArrayLit &) {

    markBeyond(0, "an array literal outside a declaration");
    expr_.reset();
}

void SToC::visit(shalimar::Blank &) {
    expr_ = intLit(0);
}

void SToC::visit(shalimar::Index &node) {
    int rankLeft = 0;
    const Info *info = nullptr;
    CExprPtr flat = linearIndex(node, &rankLeft, &info);
    if (flat == nullptr) {
        markBeyond(0, "indexing something this converter cannot see as an array");
        expr_.reset();
        return;
    }
    if (rankLeft != 0) {

        markBeyond(0, "a partial index used as a value");
        expr_.reset();
        return;
    }
    expr_ = std::move(flat);
}

void SToC::visit(shalimar::Dim &node) {
    long long axis = 0;
    if (!foldInt(*node.axis(), &axis)) {
        markBeyond(0, "'.dim' with a non-constant axis");
        expr_.reset();
        return;
    }
    shalimar::Var *var = dynamic_cast<shalimar::Var *>(node.base().get());
    const Info *info = var != nullptr ? lookupVar(*var) : nullptr;
    if (info == nullptr || info->rank == 0 ||
        axis >= static_cast<long long>(info->rank)) {
        markBeyond(0, "'." + node.spelling() + "' on something without that dimension");
        expr_.reset();
        return;
    }
    expr_ = dimValue(*info, static_cast<int>(axis));
}

void SToC::visit(shalimar::Precision &) {

    markBeyond(0, "'prec' outside a print list");
    expr_.reset();
}

void SToC::visit(shalimar::Call &node) {

    std::unique_ptr<CCall> call;

    if (node.builtin() >= 0) {
        const std::string &name = node.callee();
        const shalimar::Type *result = node.type();
        const bool intCall =
            result != nullptr && result->kind() == shalimar::Type::Kind::Int;

        if (name == "len") {
            shalimar::Var *var =
                dynamic_cast<shalimar::Var *>(node.arguments()[0].get());
            const Info *info = var != nullptr ? lookupVar(*var) : nullptr;
            if (info == nullptr || info->rank == 0) {
                markBeyond(node.line(), "'len' of something not seen as an array");
                expr_.reset();
                return;
            }
            expr_ = dimValue(*info, 0);
            return;
        }

        std::string cName;
        if (name == "abs") cName = intCall ? "abs" : "fabs";
        else if (name == "min") cName = intCall ? "c2s_min_int" : "c2s_min_real";
        else if (name == "max") cName = intCall ? "c2s_max_int" : "c2s_max_real";
        else if (name == "round") cName = "c2s_round";
        else if (name == "trunc") cName = "c2s_trunc";
        else if (name == "hypot") cName = "c2s_hypot";
        else cName = name;

        if (cName.compare(0, 4, "c2s_") == 0) {
            need(cName);
        } else if (cName == "abs") {
            usesStdlib_ = true;
        } else {
            usesMath_ = true;
        }
        call.reset(new CCall(ident(cName)));
    } else {
        call.reset(new CCall(ident(sanitise(node.callee()))));
    }

    const shalimar::Prototype *proto = node.prototype();
    std::vector<shalimar::ExprPtr> &arguments = node.arguments();
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        shalimar::Expr &argument = *arguments[i];
        const shalimar::Type *type = argument.type();

        if (type != nullptr && type->isArray()) {
            shalimar::Var *var = dynamic_cast<shalimar::Var *>(&argument);
            const Info *info = var != nullptr ? lookupVar(*var) : nullptr;
            if (info == nullptr || info->rank == 0) {
                markBeyond(node.line(), "an array argument that is not a plain name");
                expr_.reset();
                return;
            }
            call->add(ident(info->cName));
            for (int axis = 0; axis < info->rank; ++axis) {
                call->add(dimValue(*info, axis));
            }
            continue;
        }

        const bool byReference =
            proto != nullptr && i < proto->inputs.size() &&
            proto->inputs[i].byReference &&
            (proto->inputs[i].type == nullptr || !proto->inputs[i].type->isArray());
        if (byReference) {
            call->add(CExprPtr(new CUnary("&", true, expression(argument))));
            continue;
        }

        call->add(expression(argument));
    }

    expr_.reset(call.release());
}

void SToC::statement(shalimar::Stmt &node) {

    if (node.line() > 0) currentLine_ = node.line();
    node.accept(*this);
}

void SToC::block(shalimar::Block &body, CCompound &into) {
    CCompound *saved = block_;
    block_ = &into;
    for (std::size_t i = 0; i < body.size(); ++i) statement(*body[i]);
    block_ = saved;
}

void SToC::visit(shalimar::Declare &node) {
    const shalimar::Symbol *symbol = node.symbol();
    Info info;
    info.cName = sanitise(node.name());
    info.rank = static_cast<int>(node.extents().size());

    if (info.rank == 0) {
        symbols_[symbol] = info;
        CExprPtr init;
        if (node.initial() != nullptr) init = expression(*node.initial());
        block_->add(declStmtFor(info.cName, scalarC(node.declaredType()), std::move(init)));
        return;
    }

    long long total = 1;
    for (std::size_t i = 0; i < node.extents().size(); ++i) {
        long long extent = 0;
        if (!foldInt(*node.extents()[i], &extent) || extent < 1) {
            markBeyond(node.line(),
                       "an array extent that is not a constant - C89 sizes arrays at "
                       "compile time");
            symbols_[symbol] = info;
            return;
        }
        info.extents.push_back(extent);
        total *= extent;
    }
    symbols_[symbol] = info;

    CTypePtr arrayType(new CType(CType::Kind::Array));
    arrayType->setLength(std::unique_ptr<CNode>(intLit(total).release()));
    arrayType->setBase(scalarC(node.declaredType()));

    std::unique_ptr<CDeclaration> decl(new CDeclaration());
    CDeclaration::Declarator declarator;
    declarator.name = info.cName;
    declarator.type = std::move(arrayType);

    if (node.initial() != nullptr) {
        if (shalimar::StrLit *text =
                dynamic_cast<shalimar::StrLit *>(node.initial().get())) {
            declarator.init.reset(new CInit(CExprPtr(new CStringLit(
                text->text(), "\"" + cEscape(text->text()) + "\""))));
        } else if (shalimar::ArrayLit *literal =
                       dynamic_cast<shalimar::ArrayLit *>(node.initial().get())) {

            std::unique_ptr<CInit> init(new CInit());
            struct Flattener {
                SToC *self;
                void run(shalimar::ArrayLit &lit, CInit &into) {
                    std::vector<shalimar::ExprPtr> &elements = lit.elements();
                    for (std::size_t i = 0; i < elements.size(); ++i) {
                        if (shalimar::ArrayLit *inner =
                                dynamic_cast<shalimar::ArrayLit *>(elements[i].get())) {
                            run(*inner, into);
                        } else {
                            into.add(CInit(self->expression(*elements[i])));
                        }
                    }
                }
            } flattener{this};
            flattener.run(*literal, *init);
            declarator.init = std::move(init);
        } else {
            markBeyond(node.line(), "an array initialised from something that is "
                                    "not a literal");
        }
    } else {

        std::unique_ptr<CInit> init(new CInit());
        init->add(CInit(intLit(0)));
        declarator.init = std::move(init);
    }

    decl->add(std::move(declarator));
    block_->add(CStmtPtr(new CDeclStmt(std::move(decl))));
}

void SToC::visit(shalimar::Assign &node) {

    const shalimar::Type *type = node.target()->type();
    if (type != nullptr && type->isArray()) {
        shalimar::ArrayLit *literal =
            dynamic_cast<shalimar::ArrayLit *>(node.expr().get());
        const Info *info = node.symbol() != nullptr ? infoFor(node.symbol()) : nullptr;
        if (literal == nullptr || info == nullptr || info->rank == 0) {
            markBeyond(node.line(), "assigning a whole array");
            return;
        }
        std::vector<shalimar::Expr *> leaves;
        flattenLiteral(*literal, &leaves);
        for (std::size_t i = 0; i < leaves.size(); ++i) {
            CExprPtr place(new CIndex(ident(info->cName),
                                      intLit(static_cast<long long>(i))));
            CExprPtr value = expression(*leaves[i]);
            block_->add(CStmtPtr(new CExprStmt(CExprPtr(
                new CAssign("=", std::move(place), std::move(value))))));
        }
        return;
    }

    if (node.creates() && node.symbol() != nullptr &&
        symbols_.find(node.symbol()) == symbols_.end()) {

        Info info;
        info.cName = sanitise(node.symbol()->name());
        symbols_[node.symbol()] = info;
    }

    CExprPtr target = expression(*node.target());
    CExprPtr value = expression(*node.expr());
    CExprPtr assign(new CAssign("=", std::move(target), std::move(value)));
    block_->add(CStmtPtr(new CExprStmt(std::move(assign))));
}

void SToC::visit(shalimar::CompoundAssign &node) {
    CExprPtr target = expression(*node.target());
    CExprPtr value = expression(*node.expr());
    CExprPtr assign(new CAssign(node.isAdd() ? "+=" : "-=",
                                std::move(target), std::move(value)));
    block_->add(CStmtPtr(new CExprStmt(std::move(assign))));
}

void SToC::printItem(shalimar::Expr &item) {
    if (shalimar::Precision *precision = dynamic_cast<shalimar::Precision *>(&item)) {
        std::vector<CExprPtr> args;
        args.push_back(expression(*precision->places()));
        block_->add(CStmtPtr(new CExprStmt(
            callHelper("c2s_print_places", std::move(args)))));
        return;
    }

    if (shalimar::StrLit *text = dynamic_cast<shalimar::StrLit *>(&item)) {
        std::vector<CExprPtr> args;
        args.push_back(CExprPtr(new CStringLit(
            text->text(), "\"" + cEscape(text->text()) + "\"")));
        block_->add(CStmtPtr(new CExprStmt(
            callHelper("c2s_print_string", std::move(args)))));
        return;
    }

    const shalimar::Type *type = item.type();
    if (type != nullptr && type->isArray()) {

        const Info *info = nullptr;
        CExprPtr data;
        int rankLeft = 0;
        if (shalimar::Var *var = dynamic_cast<shalimar::Var *>(&item)) {
            info = lookupVar(*var);
            if (info != nullptr) {
                data = ident(info->cName);
                rankLeft = info->rank;
            }
        } else if (shalimar::Index *index = dynamic_cast<shalimar::Index *>(&item)) {
            data = linearIndex(*index, &rankLeft, &info);
        }
        if (info == nullptr || data == nullptr || rankLeft == 0) {
            markBeyond(0, "printing an array this converter cannot see whole");
            return;
        }

        const shalimar::Type::Kind kind = type->scalar()->kind();
        const int firstAxis = info->rank - rankLeft;
        if (kind == shalimar::Type::Kind::Char) {
            std::vector<CExprPtr> args;
            args.push_back(std::move(data));
            args.push_back(dimValue(*info, firstAxis));
            block_->add(CStmtPtr(new CExprStmt(
                callHelper("c2s_print_text", std::move(args)))));
            return;
        }

        const char *helper =
            kind == shalimar::Type::Kind::Real
                ? (rankLeft == 1 ? "c2s_print_grid1_real" : "c2s_print_grid2_real")
                : (rankLeft == 1 ? "c2s_print_grid1_int" : "c2s_print_grid2_int");
        if (rankLeft > 2) {
            markBeyond(0, "printing an array of rank above two");
            return;
        }
        std::vector<CExprPtr> args;
        args.push_back(std::move(data));
        for (int axis = firstAxis; axis < info->rank; ++axis) {
            args.push_back(dimValue(*info, axis));
        }
        block_->add(CStmtPtr(new CExprStmt(callHelper(helper, std::move(args)))));
        return;
    }

    const char *helper = "c2s_print_int";
    if (type != nullptr && type->kind() == shalimar::Type::Kind::Real) {
        helper = "c2s_print_real";
    } else if (type != nullptr && type->kind() == shalimar::Type::Kind::Char) {
        helper = "c2s_print_char";
    }
    std::vector<CExprPtr> args;
    args.push_back(expression(item));
    block_->add(CStmtPtr(new CExprStmt(callHelper(helper, std::move(args)))));
}

void SToC::visit(shalimar::Print &node) {
    std::vector<shalimar::ExprPtr> &items = node.items();
    for (std::size_t i = 0; i < items.size(); ++i) printItem(*items[i]);
    if (node.newline()) {
        block_->add(CStmtPtr(new CExprStmt(
            callHelper("c2s_line_end", std::vector<CExprPtr>()))));
    }
}

void SToC::visit(shalimar::If &node) {

    std::vector<shalimar::If::Branch> &branches = node.branches();

    CStmtPtr elseArm;
    if (node.hasElse()) {
        std::unique_ptr<CCompound> body(new CCompound());
        block(node.elseBody(), *body);
        elseArm.reset(body.release());
    }
    for (std::size_t i = branches.size(); i > 0; --i) {
        shalimar::If::Branch &branch = branches[i - 1];
        CExprPtr cond = expression(*branch.condition);
        std::unique_ptr<CCompound> body(new CCompound());
        block(branch.body, *body);
        CStmtPtr arm(new CIf(std::move(cond), CStmtPtr(body.release()),
                             std::move(elseArm)));
        elseArm = std::move(arm);
    }
    block_->add(std::move(elseArm));
}

void SToC::visit(shalimar::While &node) {
    CExprPtr cond = expression(*node.condition());
    std::unique_ptr<CCompound> body(new CCompound());
    block(node.body(), *body);
    block_->add(CStmtPtr(new CWhile(std::move(cond), CStmtPtr(body.release()))));
}

void SToC::visit(shalimar::For &node) {

    const shalimar::Symbol *counter = node.counter();
    const bool realCounter =
        counter != nullptr &&
        counter->type()->kind() == shalimar::Type::Kind::Real;

    Info info;
    info.cName = sanitise(node.variable());
    if (counter != nullptr) symbols_[counter] = info;

    std::unique_ptr<CCompound> wrap(new CCompound());
    CTypePtr counterType = basic(realCounter ? CType::Kind::Double : CType::Kind::Int);

    const std::string startName = freshName("from");
    const std::string endName = freshName("to");
    const std::string stepName = freshName("by");

    CCompound *savedBlock = block_;
    block_ = wrap.get();

    wrap->add(declStmtFor(info.cName, counterType->clone(), nullptr));
    wrap->add(declStmtFor(startName, counterType->clone(), nullptr));
    wrap->add(declStmtFor(endName, counterType->clone(), nullptr));
    wrap->add(declStmtFor(stepName, counterType->clone(), nullptr));

    std::string passName;
    if (realCounter) {
        passName = freshName("pass");
        CTypePtr longType = basic(CType::Kind::Int);
        longType->setLong();
        wrap->add(declStmtFor(passName, std::move(longType), nullptr));
    }

    wrap->add(CStmtPtr(new CExprStmt(CExprPtr(new CAssign(
        "=", ident(startName), expression(*node.start()))))));
    wrap->add(CStmtPtr(new CExprStmt(CExprPtr(new CAssign(
        "=", ident(endName), expression(*node.end()))))));
    wrap->add(CStmtPtr(new CExprStmt(CExprPtr(new CAssign(
        "=", ident(stepName),
        node.step() != nullptr ? expression(*node.step()) : intLit(1))))));

    CExprPtr keepGoing(new CTernary(
        CExprPtr(new CBinary(">=", ident(stepName), intLit(0))),
        CExprPtr(new CBinary("<=", ident(info.cName), ident(endName))),
        CExprPtr(new CBinary(">=", ident(info.cName), ident(endName)))));

    std::unique_ptr<CCompound> body(new CCompound());
    block(node.body(), *body);

    if (!realCounter) {
        CStmtPtr init(new CExprStmt(CExprPtr(new CAssign(
            "=", ident(info.cName), ident(startName)))));
        CExprPtr step(new CAssign("+=", ident(info.cName), ident(stepName)));
        wrap->add(CStmtPtr(new CFor(std::move(init), std::move(keepGoing),
                                    std::move(step), CStmtPtr(body.release()))));
    } else {

        std::unique_ptr<CCompound> loop(new CCompound());
        loop->add(CStmtPtr(new CExprStmt(CExprPtr(new CAssign(
            "=", ident(info.cName),
            CExprPtr(new CBinary("+", ident(startName),
                CExprPtr(new CBinary("*",
                    CExprPtr(new CCast(basic(CType::Kind::Double), ident(passName))),
                    ident(stepName))))))))));
        CExprPtr stop(new CUnary("!", true, std::move(keepGoing)));
        loop->add(CStmtPtr(new CIf(std::move(stop), CStmtPtr(new CBreak()), nullptr)));
        std::vector<CStmtPtr> &bodyItems = body->body();
        for (std::size_t i = 0; i < bodyItems.size(); ++i) {
            loop->add(std::move(bodyItems[i]));
        }

        CStmtPtr init(new CExprStmt(CExprPtr(new CAssign(
            "=", ident(passName), intLit(0)))));
        CExprPtr step(new CUnary("++", true, ident(passName)));
        wrap->add(CStmtPtr(new CFor(std::move(init), nullptr, std::move(step),
                                    CStmtPtr(loop.release()))));
    }

    block_ = savedBlock;
    block_->add(CStmtPtr(wrap.release()));
}

void SToC::visit(shalimar::Break &) {
    block_->add(CStmtPtr(new CBreak()));
}

void SToC::visit(shalimar::Continue &) {
    block_->add(CStmtPtr(new CContinue()));
}

void SToC::visit(shalimar::Return &node) {
    std::vector<shalimar::ExprPtr> &exprs = node.exprs();
    if (exprs.size() <= 1) {
        CExprPtr value;
        if (exprs.size() == 1) value = expression(*exprs[0]);

        if (currentIsMain_ && value == nullptr) value = intLit(0);
        block_->add(CStmtPtr(new CReturn(std::move(value))));
        return;
    }

    for (std::size_t i = 0; i < exprs.size(); ++i) {
        char name[24];
        std::snprintf(name, sizeof name, "c2s_out%d", static_cast<int>(i) + 1);
        CExprPtr place(new CUnary("*", true, ident(name)));
        CExprPtr store(new CAssign("=", std::move(place), expression(*exprs[i])));
        block_->add(CStmtPtr(new CExprStmt(std::move(store))));
    }
    block_->add(CStmtPtr(new CReturn(nullptr)));
}

void SToC::visit(shalimar::MultiAssign &node) {
    shalimar::Call *call = dynamic_cast<shalimar::Call *>(node.call().get());
    if (call == nullptr) {
        markBeyond(node.line(), "a multi-assignment from something that is not a call");
        return;
    }

    std::vector<const shalimar::Symbol *> &targets = node.targets();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (targets[i] != nullptr && symbols_.find(targets[i]) == symbols_.end()) {
            Info info;
            info.cName = sanitise(targets[i]->name());
            symbols_[targets[i]] = info;
        }
    }

    CExprPtr converted = expression(*call);
    CCall *ccall = dynamic_cast<CCall *>(converted.get());
    if (ccall == nullptr) return;

    const std::vector<std::string> &names = node.names();
    for (std::size_t i = 0; i < names.size(); ++i) {
        const Info *info = (i < targets.size() && targets[i] != nullptr)
                               ? infoFor(targets[i]) : nullptr;
        const std::string cName = info != nullptr ? info->cName : sanitise(names[i]);
        ccall->add(CExprPtr(new CUnary("&", true, ident(cName))));
    }
    block_->add(CStmtPtr(new CExprStmt(std::move(converted))));
}

void SToC::visit(shalimar::CallStmt &node) {
    shalimar::Call *call = dynamic_cast<shalimar::Call *>(node.call().get());
    if (call != nullptr && call->prototype() != nullptr &&
        call->prototype()->outputs.size() > 1) {
        markBeyond(node.line(),
                   "discarding the outputs of a multi-output function");
        return;
    }
    CExprPtr converted = expression(*node.call());
    block_->add(CStmtPtr(new CExprStmt(std::move(converted))));
}

void SToC::convertFunction(shalimar::Function &fn) {
    currentFn_ = &fn;
    const shalimar::Prototype &proto = fn.proto();

    CTypePtr fnType(new CType(CType::Kind::Function));
    if (proto.outputs.size() == 1) {
        fnType->setBase(scalarC(proto.outputs[0]));
    } else {
        fnType->setBase(basic(CType::Kind::Void));
    }
    const bool isMain = proto.name == "main";
    currentIsMain_ = isMain;
    if (isMain) fnType->setBase(basic(CType::Kind::Int));
    if (proto.inputs.empty() && proto.outputs.size() <= 1) fnType->setProtoVoid();

    paramInfos_.clear();
    for (std::size_t i = 0; i < proto.inputs.size(); ++i) {
        const shalimar::Param &param = proto.inputs[i];
        Info info;
        info.cName = sanitise(param.name);
        const int rank = param.type != nullptr ? param.type->rank() : 0;
        info.rank = rank;

        if (rank > 0) {
            info.isParamArray = true;
            CType::Param flat;
            flat.name = info.cName;
            flat.type = pointerTo(scalarC(param.type));
            fnType->params().push_back(std::move(flat));
            for (int axis = 0; axis < rank; ++axis) {
                char suffix[16];
                std::snprintf(suffix, sizeof suffix, "_d%d", axis);
                CType::Param dim;
                dim.name = info.cName + suffix;
                dim.type = basic(CType::Kind::Int);
                fnType->params().push_back(std::move(dim));
            }
        } else if (param.byReference) {
            info.isRefScalar = true;
            CType::Param ref;
            ref.name = info.cName;
            ref.type = pointerTo(scalarC(param.type));
            fnType->params().push_back(std::move(ref));
        } else {
            CType::Param plain;
            plain.name = info.cName;
            plain.type = scalarC(param.type);
            fnType->params().push_back(std::move(plain));
        }
        paramInfos_[param.name] = info;
    }

    if (proto.outputs.size() > 1) {
        for (std::size_t i = 0; i < proto.outputs.size(); ++i) {
            char name[24];
            std::snprintf(name, sizeof name, "c2s_out%d", static_cast<int>(i) + 1);
            CType::Param out;
            out.name = name;
            out.type = pointerTo(scalarC(proto.outputs[i]));
            fnType->params().push_back(std::move(out));
        }
    }

    std::unique_ptr<CCompound> body(new CCompound());

    struct Hoist : public shalimar::NodeVisitor {
        SToC *self;
        CCompound *top;
        bool collecting;
        std::map<const shalimar::Symbol *, bool> declared;
        std::map<const shalimar::Symbol *, bool> seen;

        void consider(const shalimar::Symbol *symbol) {
            if (symbol == nullptr || seen.count(symbol) != 0) return;
            if (declared.count(symbol) != 0) return;
            seen[symbol] = true;
            Info info;
            info.cName = SToC::sanitise(symbol->name());
            self->symbols_[symbol] = info;
            top->add(self->declStmtFor(info.cName,
                                       self->scalarC(symbol->type()), nullptr));
        }
        void walk(shalimar::Block &body) {
            for (std::size_t i = 0; i < body.size(); ++i) body[i]->accept(*this);
        }
        void visit(shalimar::Declare &node) override {
            if (collecting && node.symbol() != nullptr) declared[node.symbol()] = true;
        }
        void visit(shalimar::Assign &node) override {
            if (collecting || !node.creates()) return;
            const shalimar::Symbol *symbol = node.symbol();
            shalimar::ArrayLit *literal =
                dynamic_cast<shalimar::ArrayLit *>(node.expr().get());
            if (symbol != nullptr && symbol->type() != nullptr &&
                symbol->type()->isArray() && literal != nullptr) {
                if (seen.count(symbol) != 0 || declared.count(symbol) != 0) return;
                seen[symbol] = true;
                Info info;
                info.cName = SToC::sanitise(symbol->name());
                shapeOfLiteral(*literal, &info.extents);
                info.rank = static_cast<int>(info.extents.size());
                long long total = 1;
                for (std::size_t i = 0; i < info.extents.size(); ++i) {
                    total *= info.extents[i];
                }
                self->symbols_[symbol] = info;
                CTypePtr arrayType(new CType(CType::Kind::Array));
                arrayType->setLength(std::unique_ptr<CNode>(intLit(total).release()));
                arrayType->setBase(self->scalarC(symbol->type()));
                std::unique_ptr<CDeclaration> decl(new CDeclaration());
                CDeclaration::Declarator declarator;
                declarator.name = info.cName;
                declarator.type = std::move(arrayType);
                decl->add(std::move(declarator));
                top->add(CStmtPtr(new CDeclStmt(std::move(decl))));
                return;
            }
            consider(symbol);
        }
        void visit(shalimar::MultiAssign &node) override {
            if (collecting) return;
            std::vector<const shalimar::Symbol *> &targets = node.targets();
            for (std::size_t i = 0; i < targets.size(); ++i) consider(targets[i]);
        }
        void visit(shalimar::If &node) override {
            std::vector<shalimar::If::Branch> &branches = node.branches();
            for (std::size_t i = 0; i < branches.size(); ++i) walk(branches[i].body);
            if (node.hasElse()) walk(node.elseBody());
        }
        void visit(shalimar::While &node) override { walk(node.body()); }
        void visit(shalimar::For &node) override { walk(node.body()); }
        void visit(shalimar::IntLit &) override {}
        void visit(shalimar::RealLit &) override {}
        void visit(shalimar::Var &) override {}
        void visit(shalimar::Convert &) override {}
        void visit(shalimar::Binary &) override {}
        void visit(shalimar::CompoundAssign &) override {}
        void visit(shalimar::Print &) override {}
        void visit(shalimar::Break &) override {}
        void visit(shalimar::Continue &) override {}
        void visit(shalimar::Call &) override {}
        void visit(shalimar::Return &) override {}
        void visit(shalimar::CallStmt &) override {}
        void visit(shalimar::StrLit &) override {}
        void visit(shalimar::ArrayLit &) override {}
        void visit(shalimar::Blank &) override {}
        void visit(shalimar::Index &) override {}
        void visit(shalimar::Dim &) override {}
        void visit(shalimar::Precision &) override {}
    } hoist;
    hoist.self = this;
    hoist.top = body.get();
    hoist.collecting = true;
    hoist.walk(fn.body());
    hoist.collecting = false;
    hoist.walk(fn.body());

    block(fn.body(), *body);

    if (isMain) {
        body->add(CStmtPtr(new CReturn(intLit(0))));
    }

    program_->add(std::unique_ptr<CFunctionDef>(new CFunctionDef(
        isMain ? "main" : sanitise(proto.name), std::move(fnType),
        std::move(body), false)));
    currentFn_ = nullptr;
    currentIsMain_ = false;
}

std::unique_ptr<CProgram> SToC::convert(shalimar::Program &program) {
    program_.reset(new CProgram());
    sProgram_ = &program;

    const std::vector<shalimar::Program::Entry> &order = program.order();
    for (std::size_t i = 0; i < order.size(); ++i) {
        const shalimar::Program::Entry &entry = order[i];
        if (entry.isFunction) {
            shalimar::Function &fn = *program.functions()[entry.index];
            if (fn.isRejected()) continue;
            convertFunction(fn);
        } else {

            shalimar::Stmt &global = *program.globals()[entry.index];
            std::unique_ptr<CCompound> holder(new CCompound());
            CCompound *saved = block_;
            block_ = holder.get();
            statement(global);
            block_ = saved;
            std::vector<CStmtPtr> &made = holder->body();
            for (std::size_t k = 0; k < made.size(); ++k) {
                if (CDeclStmt *decl = dynamic_cast<CDeclStmt *>(made[k].get())) {
                    std::unique_ptr<CDeclaration> lifted(
                        new CDeclaration(std::move(decl->decl())));
                    program_->add(std::move(lifted));
                } else if (dynamic_cast<CBeyond *>(made[k].get()) != nullptr) {

                    program_->addMarker(std::move(made[k]));
                }
            }
        }
    }

    sProgram_ = nullptr;
    return std::move(program_);
}

std::vector<std::string> SToC::includes() const {
    std::vector<std::string> out;
    const bool traps = helpers_.count("c2s_add_int") != 0 ||
                       helpers_.count("c2s_sub_int") != 0 ||
                       helpers_.count("c2s_mul_int") != 0;
    if (usesPrint_ || traps) out.push_back("stdio.h");
    if (traps) out.push_back("stdlib.h");
    if (usesMath_ || helpers_.count("c2s_round") != 0 ||
        helpers_.count("c2s_trunc") != 0 || helpers_.count("c2s_hypot") != 0 ||
        helpers_.count("c2s_min_real") != 0 || helpers_.count("c2s_max_real") != 0 ||
        helpers_.count("c2s_print_real") != 0 ||
        helpers_.count("c2s_print_grid1_real") != 0 ||
        helpers_.count("c2s_print_grid2_real") != 0) {
        out.push_back("math.h");
    }
    if (helpers_.count("c2s_print_grid1_real") != 0 ||
        helpers_.count("c2s_print_grid2_real") != 0 ||
        helpers_.count("c2s_print_grid1_int") != 0 ||
        helpers_.count("c2s_print_grid2_int") != 0) {
        out.push_back("string.h");
    }
    if (usesStdlib_) out.push_back("stdlib.h");
    return out;
}

std::string SToC::preamble() const {

    std::string out;
    const bool grids = helpers_.count("c2s_print_grid1_real") != 0 ||
                       helpers_.count("c2s_print_grid2_real") != 0 ||
                       helpers_.count("c2s_print_grid1_int") != 0 ||
                       helpers_.count("c2s_print_grid2_int") != 0;

    // Each of these is emitted only where something reads it. Every helper
    // below is already gated that way; these three were not, so a program that
    // printed nothing but integers carried two decimal-place settings it never
    // consulted - and a `cc -Wall` elsewhere said so twice. Output that is
    // going to somebody else's terminal has to arrive without warnings of its
    // own, or the first thing it does is look broken.
    //
    // `c2s_line_has_text` stays with `usesPrint_`: every print helper assigns
    // it, so omitting the declaration would not compile. Only the grid helper
    // reads it.
    const bool places = helpers_.count("c2s_print_real") != 0 || grids ||
                        helpers_.count("c2s_print_places") != 0;
    if (places) out += "static int c2s_places = 7;\n";
    if (grids || helpers_.count("c2s_print_places") != 0) {
        out += "static int c2s_grid_places = 6;\n";
    }
    if (usesPrint_) out += "static int c2s_line_has_text = 0;\n";
    if (helpers_.count("c2s_print_int") != 0) {
        out +=
            "static void c2s_print_int(int v) {\n"
            "    printf(\"%d \", v);\n"
            "    c2s_line_has_text = 1;\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_real") != 0 || grids) {
        out +=
            "static void c2s_fixed(char *out, double v, int places) {\n"
            "    if (v < 1e15 && v > -1e15 && v == v) {\n"
            "        sprintf(out, \"%.*f\", places, v);\n"
            "    } else {\n"
            "        sprintf(out, \"%g\", v);\n"
            "    }\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_real") != 0) {
        out +=
            "static void c2s_print_real(double v) {\n"
            "    char text[400];\n"
            "    c2s_fixed(text, v, c2s_places);\n"
            "    printf(\"%s \", text);\n"
            "    c2s_line_has_text = 1;\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_char") != 0) {
        out +=
            "static void c2s_print_char(int c) {\n"
            "    if (c == 0) printf(\" \");\n"
            "    else printf(\"%c \", c);\n"
            "    c2s_line_has_text = 1;\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_string") != 0) {
        out +=
            "static void c2s_print_string(const char *s) {\n"
            "    printf(\"%s \", s);\n"
            "    c2s_line_has_text = 1;\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_text") != 0) {
        out +=
            "static void c2s_print_text(const char *s, int cap) {\n"
            "    int i;\n"
            "    for (i = 0; i < cap && s[i] != 0; ++i) putchar(s[i]);\n"
            "    putchar(' ');\n"
            "    c2s_line_has_text = 1;\n"
            "}\n";
    }
    if (grids) {

        out +=
            "static void c2s_grid(const double *reals, const int *ints,\n"
            "                     int rows, int cols) {\n"
            "    char cell[400];\n"
            "    int r, c;\n"
            "    unsigned int width = 0, n;\n"
            "    for (r = 0; r < rows * cols; ++r) {\n"
            "        if (reals != 0) c2s_fixed(cell, reals[r], c2s_grid_places);\n"
            "        else sprintf(cell, \"%d\", ints[r]);\n"
            "        n = (unsigned int)strlen(cell);\n"
            "        if (n > width) width = n;\n"
            "    }\n"
            "    if (rows > 1 && c2s_line_has_text) printf(\"\\n\");\n"
            "    for (r = 0; r < rows; ++r) {\n"
            "        if (r > 0) printf(\"\\n\");\n"
            "        for (c = 0; c < cols; ++c) {\n"
            "            if (reals != 0)\n"
            "                c2s_fixed(cell, reals[r * cols + c], c2s_grid_places);\n"
            "            else sprintf(cell, \"%d\", ints[r * cols + c]);\n"
            "            if (c > 0) printf(\"  \");\n"
            "            printf(\"%*s\", (int)width, cell);\n"
            "        }\n"
            "    }\n"
            "    printf(\" \");\n"
            "    c2s_line_has_text = 1;\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_grid1_real") != 0) {
        out +=
            "static void c2s_print_grid1_real(const double *a, int d0) {\n"
            "    c2s_grid(a, 0, 1, d0);\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_grid2_real") != 0) {
        out +=
            "static void c2s_print_grid2_real(const double *a, int d0, int d1) {\n"
            "    c2s_grid(a, 0, d0, d1);\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_grid1_int") != 0) {
        out +=
            "static void c2s_print_grid1_int(const int *a, int d0) {\n"
            "    c2s_grid(0, a, 1, d0);\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_grid2_int") != 0) {
        out +=
            "static void c2s_print_grid2_int(const int *a, int d0, int d1) {\n"
            "    c2s_grid(0, a, d0, d1);\n"
            "}\n";
    }
    if (helpers_.count("c2s_line_end") != 0) {
        out +=
            "static void c2s_line_end(void) {\n"
            "    putchar('\\n');\n"
            "    c2s_line_has_text = 0;\n"
            "}\n";
    }
    if (helpers_.count("c2s_print_places") != 0) {
        out +=
            "static void c2s_print_places(int requested) {\n"
            "    int places = requested < -1 ? -1 : (requested > 24 ? 24 : requested);\n"
            "    if (places < 0) { c2s_places = 7; c2s_grid_places = 6; return; }\n"
            "    c2s_places = places;\n"
            "    c2s_grid_places = places;\n"
            "}\n";
    }

    static const struct { const char *helper; const char *trap; const char *op; }
        kTraps[] = {
            {"c2s_add_int", "c2s_overflow_add", "+"},
            {"c2s_sub_int", "c2s_overflow_sub", "-"},
            {"c2s_mul_int", "c2s_overflow_mul", "*"},
        };
    for (std::size_t i = 0; i < sizeof kTraps / sizeof kTraps[0]; ++i) {
        if (helpers_.count(kTraps[i].helper) == 0) continue;

        out += std::string("static void ") + kTraps[i].trap + "(int line) {\n" +
               "    printf(\"Error: line %d: int overflow in '" + kTraps[i].op +
               "' - use real\\n\", line);\n"
               "    exit(1);\n"
               "}\n";
    }
    if (helpers_.count("c2s_add_int") != 0) {
        out +=
            "static int c2s_add_int(int a, int b, int line) {\n"
            "    if ((b > 0 && a > 2147483647 - b) ||\n"
            "        (b < 0 && a < (-2147483647 - 1) - b)) c2s_overflow_add(line);\n"
            "    return a + b;\n"
            "}\n";
    }
    if (helpers_.count("c2s_sub_int") != 0) {
        out +=
            "static int c2s_sub_int(int a, int b, int line) {\n"
            "    if ((b < 0 && a > 2147483647 + b) ||\n"
            "        (b > 0 && a < (-2147483647 - 1) + b)) c2s_overflow_sub(line);\n"
            "    return a - b;\n"
            "}\n";
    }
    if (helpers_.count("c2s_mul_int") != 0) {

        out +=
            "static int c2s_mul_int(int a, int b, int line) {\n"
            "    if (a > 0) {\n"
            "        if (b > 0) { if (a > 2147483647 / b) c2s_overflow_mul(line); }\n"
            "        else { if (b < (-2147483647 - 1) / a) c2s_overflow_mul(line); }\n"
            "    } else if (a < 0) {\n"
            "        if (b > 0) { if (a < (-2147483647 - 1) / b) c2s_overflow_mul(line); }\n"
            "        else { if (b < 2147483647 / a) c2s_overflow_mul(line); }\n"
            "    }\n"
            "    return a * b;\n"
            "}\n";
    }
    if (helpers_.count("c2s_int_pow") != 0) {
        out +=
            "static int c2s_int_pow(int a, int b) {\n"
            "    int result = 1;\n"
            "    int i;\n"
            "    for (i = 0; i < b; ++i) result = result * a;\n"
            "    return result;\n"
            "}\n";
    }
    if (helpers_.count("c2s_min_int") != 0) {
        out += "static int c2s_min_int(int a, int b) { return a < b ? a : b; }\n";
    }
    if (helpers_.count("c2s_max_int") != 0) {
        out += "static int c2s_max_int(int a, int b) { return a > b ? a : b; }\n";
    }
    if (helpers_.count("c2s_min_real") != 0) {
        out += "static double c2s_min_real(double a, double b) { return a < b ? a : b; }\n";
    }
    if (helpers_.count("c2s_max_real") != 0) {
        out += "static double c2s_max_real(double a, double b) { return a > b ? a : b; }\n";
    }
    if (helpers_.count("c2s_round") != 0) {
        out +=
            "static double c2s_round(double v) {\n"
            "    return v >= 0 ? floor(v + 0.5) : ceil(v - 0.5);\n"
            "}\n";
    }
    if (helpers_.count("c2s_trunc") != 0) {
        out +=
            "static double c2s_trunc(double v) {\n"
            "    return v >= 0 ? floor(v) : ceil(v);\n"
            "}\n";
    }
    if (helpers_.count("c2s_hypot") != 0) {
        out +=
            "static double c2s_hypot(double a, double b) {\n"
            "    return sqrt(a * a + b * b);\n"
            "}\n";
    }
    return out;
}

}
