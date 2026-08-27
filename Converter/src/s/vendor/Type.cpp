#include "Type.h"

#include <map>

namespace shalimar {

std::string Type::spelling() const {
    switch (kind_) {
    case Kind::Int:   return "int";
    case Kind::Real:  return "real";
    case Kind::Char:  return "char";
    case Kind::Array: return element_->spelling() + "[]";
    }
    return "int";
}

const Type *Type::intType() {
    static const Type t(Kind::Int, nullptr);
    return &t;
}

const Type *Type::realType() {
    static const Type t(Kind::Real, nullptr);
    return &t;
}

const Type *Type::charType() {
    static const Type t(Kind::Char, nullptr);
    return &t;
}

const Type *Type::arrayOf(const Type *element) {
    static std::map<const Type *, const Type *> made;
    std::map<const Type *, const Type *>::iterator found = made.find(element);
    if (found != made.end()) return found->second;
    const Type *created = new Type(Kind::Array, element);
    made[element] = created;
    return created;
}

}
