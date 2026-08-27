
#pragma once

#include "Type.h"

#include <string>

namespace shalimar {

struct Builtin {

    enum class Shape {
        Real,
        IntOrReal,
        Length
    };

    const char *name;
    int arity;
    Shape shape;
    const char *realSymbol;
    const char *intSymbol;
};

int findBuiltin(const std::string &name);
// Why a name a person is likely to try cannot be borrowed, or null.
const char *whyNotBorrowable(const std::string &name);
const Builtin &builtin(int index);
int builtinCount();

bool isConstant(const std::string &name);
double constantValue(const std::string &name);

}
