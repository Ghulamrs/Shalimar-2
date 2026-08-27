#include "CPrinter.h"

namespace c2s {

CPrinter::CPrinter() : depth_(0), floor_(PrecComma) {}

int CPrinter::precedenceOfBinary(const std::string &op) {
    if (op == "*" || op == "/" || op == "%") return PrecMultiplicative;
    if (op == "+" || op == "-") return PrecAdditive;
    if (op == "<<" || op == ">>") return PrecShift;
    if (op == "<" || op == ">" || op == "<=" || op == ">=") return PrecRelational;
    if (op == "==" || op == "!=") return PrecEquality;
    if (op == "&") return PrecBitAnd;
    if (op == "^") return PrecBitXor;
    if (op == "|") return PrecBitOr;
    if (op == "&&") return PrecAnd;
    if (op == "||") return PrecOr;
    return PrecComma;
}

void CPrinter::indent() {
    for (int i = 0; i < depth_; ++i) out_ += "    ";
}

void CPrinter::expr(CExpr &node, int floor) {
    const int saved = floor_;
    floor_ = floor;
    node.accept(*this);
    floor_ = saved;
}

std::string CPrinter::specifierText(const CType &type) {
    std::string text;
    if (type.isConst()) text += "const ";
    if (type.isVolatile()) text += "volatile ";

    switch (type.kind()) {
        case CType::Kind::Void: return text + "void";
        case CType::Kind::Char:
        case CType::Kind::Int:
        case CType::Kind::Float:
        case CType::Kind::Double: {
            if (type.isSignedExplicit()) text += "signed ";
            if (type.isUnsigned()) text += "unsigned ";
            if (type.isShort()) text += "short ";
            if (type.isLong()) text += "long ";
            switch (type.kind()) {
                case CType::Kind::Char:   return text + "char";
                case CType::Kind::Int:    return text + "int";
                case CType::Kind::Float:  return text + "float";
                case CType::Kind::Double: return text + "double";
                default: break;
            }
            return text;
        }
        case CType::Kind::Struct:
        case CType::Kind::Union: {
            text += type.kind() == CType::Kind::Struct ? "struct" : "union";
            if (!type.tag().empty()) text += " " + type.tag();
            if (type.hasMemberList()) {
                text += " {\n";
                const std::vector<CType::Member> &members = type.members();
                for (std::size_t i = 0; i < members.size(); ++i) {
                    text += "    ";
                    text += typeText(*members[i].type, members[i].name);
                    if (members[i].bitWidth != nullptr) {
                        CPrinter one;
                        text += " : " + one.printExpr(
                            static_cast<CExpr &>(*members[i].bitWidth));
                    }
                    text += ";\n";
                }
                text += "}";
            }
            return text;
        }
        case CType::Kind::Enum: {
            text += "enum";
            if (!type.tag().empty()) text += " " + type.tag();
            if (type.hasMemberList()) {
                text += " { ";
                const std::vector<CType::Enumerator> &enumerators = type.enumerators();
                for (std::size_t i = 0; i < enumerators.size(); ++i) {
                    if (i > 0) text += ", ";
                    text += enumerators[i].name;
                    if (enumerators[i].value != nullptr) {
                        CPrinter one;
                        text += " = " + one.printExpr(
                            static_cast<CExpr &>(*enumerators[i].value));
                    }
                }
                text += " }";
            }
            return text;
        }
        case CType::Kind::Named:
            return text + type.tag();
        default:
            return text;
    }
}

std::string CPrinter::typeText(const CType &type, const std::string &inner) {

    switch (type.kind()) {
        case CType::Kind::Pointer: {
            std::string text = "*";
            if (type.isConst()) text += "const ";
            if (type.isVolatile()) text += "volatile ";
            text += inner;
            const CType *base = type.base();
            if (base != nullptr && (base->kind() == CType::Kind::Array ||
                                    base->kind() == CType::Kind::Function)) {
                text = "(" + text + ")";
            }
            return typeText(*base, text);
        }
        case CType::Kind::Array: {
            std::string text = inner + "[";
            if (type.length() != nullptr) {
                CPrinter one;
                text += one.printExpr(static_cast<CExpr &>(*type.length()));
            }
            text += "]";
            return typeText(*type.base(), text);
        }
        case CType::Kind::Function: {
            std::string text = inner + "(";
            const std::vector<CType::Param> &params = type.params();
            if (params.empty()) {
                if (type.isProtoVoid()) text += "void";
            } else {
                for (std::size_t i = 0; i < params.size(); ++i) {
                    if (i > 0) text += ", ";
                    text += typeText(*params[i].type, params[i].name);
                }
                if (type.isVariadic()) text += ", ...";
            }
            text += ")";
            return typeText(*type.base(), text);
        }
        default: {
            const std::string spec = specifierText(type);
            if (inner.empty()) return spec;
            return spec + " " + inner;
        }
    }
}

std::string CPrinter::declaration(const CType &type, const std::string &name) {
    return typeText(type, name);
}

void CPrinter::visit(CIntLit &node)    { out_ += node.spelling(); }
void CPrinter::visit(CFloatLit &node)  { out_ += node.spelling(); }
void CPrinter::visit(CCharLit &node)   { out_ += node.spelling(); }
void CPrinter::visit(CStringLit &node) { out_ += node.spelling(); }
void CPrinter::visit(CIdent &node)     { out_ += node.name(); }

void CPrinter::visit(CUnary &node) {
    const bool parens = PrecUnary < floor_ && node.prefix();
    const bool postfixParens = PrecPostfix < floor_ && !node.prefix();
    if (parens || postfixParens) out_ += '(';
    if (node.prefix()) {
        out_ += node.op();

        std::string kept;
        kept.swap(out_);
        expr(node.operand(), PrecUnary);
        std::string operand;
        operand.swap(out_);
        out_.swap(kept);
        if (!operand.empty() && !node.op().empty() &&
            (operand[0] == node.op()[0])) {
            out_ += ' ';
        }
        out_ += operand;
    } else {
        expr(node.operand(), PrecPostfix);
        out_ += node.op();
    }
    if (parens || postfixParens) out_ += ')';
}

void CPrinter::visit(CBinary &node) {
    const int precedence = precedenceOfBinary(node.op());
    const bool parens = precedence < floor_;
    if (parens) out_ += '(';
    expr(node.lhs(), precedence);
    out_ += ' ';
    out_ += node.op();
    out_ += ' ';
    expr(node.rhs(), precedence + 1);
    if (parens) out_ += ')';
}

void CPrinter::visit(CAssign &node) {
    const bool parens = PrecAssign < floor_;
    if (parens) out_ += '(';
    expr(node.target(), PrecUnary);
    out_ += ' ';
    out_ += node.op();
    out_ += ' ';
    expr(node.value(), PrecAssign);
    if (parens) out_ += ')';
}

void CPrinter::visit(CTernary &node) {
    const bool parens = PrecTernary < floor_;
    if (parens) out_ += '(';
    expr(node.cond(), PrecOr);
    out_ += " ? ";
    expr(node.thenArm(), PrecComma);
    out_ += " : ";
    expr(node.elseArm(), PrecTernary);
    if (parens) out_ += ')';
}

void CPrinter::visit(CCall &node) {
    expr(node.callee(), PrecPostfix);
    out_ += '(';
    std::vector<CExprPtr> &args = node.args();
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out_ += ", ";
        expr(*args[i], PrecAssign);
    }
    out_ += ')';
}

void CPrinter::visit(CIndex &node) {
    expr(node.base(), PrecPostfix);
    out_ += '[';
    expr(node.index(), PrecComma);
    out_ += ']';
}

void CPrinter::visit(CMember &node) {
    expr(node.object(), PrecPostfix);
    out_ += node.arrow() ? "->" : ".";
    out_ += node.name();
}

void CPrinter::visit(CCast &node) {
    const bool parens = PrecUnary < floor_;
    if (parens) out_ += '(';
    out_ += '(';
    out_ += typeText(node.type(), "");
    out_ += ')';
    expr(node.operand(), PrecUnary);
    if (parens) out_ += ')';
}

void CPrinter::visit(CSizeof &node) {
    const bool parens = PrecUnary < floor_;
    if (parens) out_ += '(';
    if (node.ofType()) {
        out_ += "sizeof(";
        out_ += typeText(*node.type(), "");
        out_ += ')';
    } else {
        out_ += "sizeof ";
        expr(*node.operand(), PrecUnary);
    }
    if (parens) out_ += ')';
}

void CPrinter::visit(CComma &node) {
    const bool parens = PrecComma < floor_ || floor_ > PrecComma;
    if (parens) out_ += '(';
    expr(node.left(), PrecComma);
    out_ += ", ";
    expr(node.right(), PrecAssign);
    if (parens) out_ += ')';
}

void CPrinter::printInit(CInit &init) {
    if (!init.isList()) {
        expr(*init.expr(), PrecAssign);
        return;
    }
    out_ += "{ ";
    std::vector<CInit> &items = init.items();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out_ += ", ";
        printInit(items[i]);
    }
    out_ += " }";
}

void CPrinter::printDecl(CDeclaration &decl) {
    switch (decl.storage()) {
        case CDeclaration::Storage::Typedef:  out_ += "typedef "; break;
        case CDeclaration::Storage::Extern:   out_ += "extern "; break;
        case CDeclaration::Storage::Static:   out_ += "static "; break;
        case CDeclaration::Storage::Auto:     out_ += "auto "; break;
        case CDeclaration::Storage::Register: out_ += "register "; break;
        case CDeclaration::Storage::None:     break;
    }

    if (decl.bareType() != nullptr) {
        out_ += specifierText(*decl.bareType());
        out_ += ';';
        return;
    }

    std::vector<CDeclaration::Declarator> &declarators = decl.declarators();
    for (std::size_t i = 0; i < declarators.size(); ++i) {
        if (i > 0) out_ += ", ";
        if (i == 0) {
            out_ += typeText(*declarators[i].type, declarators[i].name);
        } else {

            std::string whole = typeText(*declarators[i].type, declarators[i].name);
            const std::string spec = specifierText([&]() -> const CType & {
                const CType *walk = declarators[i].type.get();
                while (walk->base() != nullptr) walk = walk->base();
                return *walk;
            }());
            if (whole.compare(0, spec.size(), spec) == 0) {
                std::size_t from = spec.size();
                while (from < whole.size() && whole[from] == ' ') ++from;
                whole = whole.substr(from);
            }
            out_ += whole;
        }
        if (declarators[i].init != nullptr) {
            out_ += " = ";
            printInit(*declarators[i].init);
        }
    }
    out_ += ';';
}

void CPrinter::visit(CDeclStmt &node) {
    indent();
    printDecl(node.decl());
    out_ += '\n';
}

void CPrinter::visit(CExprStmt &node) {
    indent();
    expr(node.expr(), PrecComma);
    out_ += ";\n";
}

void CPrinter::visit(CEmpty &) {
    indent();
    out_ += ";\n";
}

void CPrinter::visit(CCompound &node) {
    indent();
    out_ += "{\n";
    ++depth_;
    std::vector<CStmtPtr> &body = node.body();
    for (std::size_t i = 0; i < body.size(); ++i) stmt(*body[i]);
    --depth_;
    indent();
    out_ += "}\n";
}

void CPrinter::child(CStmt &node) {

    if (dynamic_cast<CCompound *>(&node) != nullptr) {
        stmt(node);
    } else {
        ++depth_;
        stmt(node);
        --depth_;
    }
}

// The `if` an `else` arm chains to, or null.
//
// Two shapes reach here and both are the same C. `else if (c) ...` parses as
// an else arm that IS a CIf. `else { if (c) ... }` parses as a compound
// holding one, and a Shalimar `else` block whose only statement is an `if`
// lowers to exactly that - which is how SToC kept producing the stepped
// `else { if ... }` the flat chain above was meant to remove.
//
// **Only when the block holds that `if` and nothing else.** A second statement
// makes the braces load-bearing: `else { if (c) A B }` runs B whatever c is,
// and `else if (c) A B` does not - it is not even the same parse. A single
// declaration is the same trap wearing a type. So the count is checked before
// the kind, and one is the only count that qualifies.
static CIf *chainedIf(CStmt *arm) {
    if (arm == nullptr) return nullptr;
    if (CIf *direct = dynamic_cast<CIf *>(arm)) return direct;
    CCompound *block = dynamic_cast<CCompound *>(arm);
    if (block == nullptr || block->body().size() != 1) return nullptr;
    return dynamic_cast<CIf *>(block->body()[0].get());
}

void CPrinter::visit(CIf &node) {
    // **`else if` on one line, not `else` wrapping a nested `if`.** Both are
    // the same C and the same tree; only one of them is readable. The nested
    // form indents a step per branch, so the five-branch chain that CToS
    // writes as a flat `else if` list came back from SToC indented six levels
    // and drifting right - the two directions did not describe the same shape
    // even though they meant it.
    //
    // Written as a loop rather than by recursing, because recursing is what
    // produced the indentation: the chain is one construct in C's own idiom,
    // and this walks it as one. It also means a chain of any length costs no
    // stack here, which the old form did.
    //
    // The `else` arm is chained ONLY when it is an `if` and nothing else. An
    // arm the source wrapped in braces is a CCompound, not a CIf, so it still
    // gets `else` and its own block - the braces were the author's and are
    // kept.
    indent();
    CIf *current = &node;
    for (;;) {
        out_ += "if (";
        expr(current->cond(), PrecComma);
        out_ += ")\n";
        child(current->thenArm());

        CStmt *otherwise = current->elseArm();
        if (otherwise == nullptr) return;

        indent();
        CIf *chained = chainedIf(otherwise);
        if (chained == nullptr) {
            out_ += "else\n";
            child(*otherwise);
            return;
        }
        out_ += "else ";
        current = chained;
    }
}

void CPrinter::visit(CWhile &node) {
    indent();
    out_ += "while (";
    expr(node.cond(), PrecComma);
    out_ += ")\n";
    child(node.body());
}

void CPrinter::visit(CDoWhile &node) {
    indent();
    out_ += "do\n";
    child(node.body());
    indent();
    out_ += "while (";
    expr(node.cond(), PrecComma);
    out_ += ");\n";
}

void CPrinter::visit(CFor &node) {
    indent();
    out_ += "for (";
    if (node.init() != nullptr) {

        if (CDeclStmt *decl = dynamic_cast<CDeclStmt *>(node.init())) {
            printDecl(decl->decl());
        } else if (CExprStmt *exprStmt = dynamic_cast<CExprStmt *>(node.init())) {
            expr(exprStmt->expr(), PrecComma);
            out_ += ';';
        }
    } else {
        out_ += ';';
    }
    out_ += ' ';
    if (node.cond() != nullptr) expr(*node.cond(), PrecComma);
    out_ += "; ";
    if (node.step() != nullptr) expr(*node.step(), PrecComma);
    out_ += ")\n";
    child(node.body());
}

void CPrinter::visit(CSwitch &node) {
    indent();
    out_ += "switch (";
    expr(node.cond(), PrecComma);
    out_ += ")\n";
    child(node.body());
}

void CPrinter::visit(CCase &node) {

    if (depth_ > 0) --depth_;
    indent();
    ++depth_;
    if (node.isDefault()) {
        out_ += "default:\n";
    } else {
        out_ += "case ";
        expr(*node.value(), PrecTernary);
        out_ += ":\n";
    }
    stmt(node.body());
}

void CPrinter::visit(CBreak &) {
    indent();
    out_ += "break;\n";
}

void CPrinter::visit(CContinue &) {
    indent();
    out_ += "continue;\n";
}

void CPrinter::visit(CReturn &node) {
    indent();
    out_ += "return";
    if (node.value() != nullptr) {
        out_ += ' ';
        expr(*node.value(), PrecComma);
    }
    out_ += ";\n";
}

void CPrinter::visit(CGoto &node) {
    indent();
    out_ += "goto ";
    out_ += node.label();
    out_ += ";\n";
}

void CPrinter::visit(CLabel &node) {

    if (depth_ > 0) --depth_;
    indent();
    ++depth_;
    out_ += node.name();
    out_ += ":\n";
    stmt(node.body());
}

void CPrinter::visit(CBeyond &node) {
    indent();
    out_ += "/* #BEYOND SHALIMAR: " + node.reason() + "\n";
    const std::vector<std::string> &lines = node.lines();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        indent();
        out_ += "   " + lines[i] + "\n";
    }
    indent();
    out_ += "*/\n";
}

void CPrinter::stmt(CStmt &node) {
    node.accept(*this);
}

std::string CPrinter::print(CProgram &program) {
    out_.clear();
    depth_ = 0;

    const std::vector<CProgram::Entry> &order = program.order();
    bool first = true;
    for (std::size_t i = 0; i < order.size(); ++i) {
        const CProgram::Entry &entry = order[i];
        if (entry.isMarker) {

            if (!first) out_ += '\n';
            stmt(*program.markers()[entry.index]);
            first = false;
            continue;
        }
        if (entry.isFunction) {
            CFunctionDef &fn = *program.functions()[entry.index];
            if (!first) out_ += '\n';
            if (fn.isStatic()) out_ += "static ";
            out_ += typeText(fn.type(), fn.name());
            out_ += '\n';
            stmt(fn.body());
        } else {
            printDecl(*program.declarations()[entry.index]);
            out_ += '\n';
        }
        first = false;
    }
    return out_;
}

std::string CPrinter::printExpr(CExpr &node) {
    std::string kept;
    kept.swap(out_);
    expr(node, PrecComma);
    std::string result;
    result.swap(out_);
    out_.swap(kept);
    return result;
}

}
