
#pragma once

#include <string>

namespace shalimar {

class Type {
public:
    enum class Kind { Int, Real, Char, Array };

    static const Type *intType();
    static const Type *realType();
    static const Type *charType();
    static const Type *arrayOf(const Type *element);

    Kind kind() const { return kind_; }
    bool isArray() const { return kind_ == Kind::Array; }
    bool isScalar() const { return kind_ != Kind::Array; }

    const Type *element() const { return element_; }

    int rank() const { return isArray() ? 1 + element_->rank() : 0; }

    const Type *scalar() const { return isArray() ? element_->scalar() : this; }

    bool isWellFormed() const {
        return !(scalar()->kind() == Kind::Char && rank() > 1);
    }

    std::string spelling() const;

private:
    Type(Kind kind, const Type *element) : kind_(kind), element_(element) {}

    Kind kind_;
    const Type *element_;
};

}
