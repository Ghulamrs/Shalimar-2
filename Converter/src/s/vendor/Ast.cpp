#include "Ast.h"

namespace shalimar {

const char *Binary::spelling(Op op) {
    switch (op) {
    case Op::Add:          return "+";
    case Op::Subtract:     return "-";
    case Op::Multiply:     return "*";
    case Op::Divide:       return "/";
    case Op::Modulus:      return "%";
    case Op::Power:        return "^";
    case Op::Equal:        return "=";
    case Op::NotEqual:     return "!=";
    case Op::Less:         return "<";
    case Op::Greater:      return ">";
    case Op::LessEqual:    return "<=";
    case Op::GreaterEqual: return ">=";
    case Op::And:          return "&";
    case Op::Or:           return "|";
    }
    return "?";
}

bool Binary::yieldsInt(Op op) {
    switch (op) {
    case Op::Equal:
    case Op::NotEqual:
    case Op::Less:
    case Op::Greater:
    case Op::LessEqual:
    case Op::GreaterEqual:
    case Op::And:
    case Op::Or:
        return true;
    default:
        return false;
    }
}

const char *Binary::runtimeFor(Op op, const Type *operands) {
    const bool real = operands->kind() == Type::Kind::Real;
    switch (op) {
    case Op::Add:          return real ? "shm_real_add" : "shm_int_add";
    case Op::Subtract:     return real ? "shm_real_sub" : "shm_int_sub";
    case Op::Multiply:     return real ? "shm_real_mul" : "shm_int_mul";
    case Op::Divide:       return real ? "shm_real_div" : "shm_int_div";
    case Op::Modulus:      return real ? "shm_real_mod" : "shm_int_mod";
    case Op::Power:        return real ? "shm_real_pow" : "shm_int_pow";
    case Op::Equal:        return real ? "shm_real_eq"  : "shm_int_eq";
    case Op::NotEqual:     return real ? "shm_real_ne"  : "shm_int_ne";
    case Op::Less:         return real ? "shm_real_lt"  : "shm_int_lt";
    case Op::Greater:      return real ? "shm_real_gt"  : "shm_int_gt";
    case Op::LessEqual:    return real ? "shm_real_le"  : "shm_int_le";
    case Op::GreaterEqual: return real ? "shm_real_ge"  : "shm_int_ge";
    case Op::And:          return real ? "shm_real_and" : "shm_int_and";
    case Op::Or:           return real ? "shm_real_or"  : "shm_int_or";
    }
    return "shm_int_add";
}

Function *Program::find(const std::string &name) {
    for (std::unique_ptr<Function> &f : functions_) {
        if (f->proto().name == name) return f.get();
    }
    return nullptr;
}

const Function *Program::find(const std::string &name) const {
    for (const std::unique_ptr<Function> &f : functions_) {
        if (f->proto().name == name) return f.get();
    }
    return nullptr;
}

}
