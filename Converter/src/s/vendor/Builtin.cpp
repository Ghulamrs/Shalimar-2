#include "Builtin.h"

#include <cmath>
#include <cstring>

namespace shalimar {
namespace {

// **The symbol each one calls.** Where it is a C library name - `sin`, `fabs` -
// shc emits a call straight to it and the linker resolves it from libm: no
// wrapper, no generated C, nothing added to the runtime archive. That is what
// lets the borrowable set grow without the archive growing. docs/FOREIGN.md.
//
// Five are NOT library calls and must not become them:
//
//   shm_fn_abs_int   traps on INT_MIN rather than negating it. C's abs() is
//                    undefined there, so this is a different function that
//                    happens to share a name.
//   shm_fn_max_real  `a > b ? a : b`, which propagates NaN. fmax() returns the
//   shm_fn_min_real  non-NaN operand instead, so swapping them changes answers.
//   shm_fn_max_int   the C library has no integer max or min at all.
//   shm_fn_min_int
//
// `len` is the array handle's own and never was C's.
const Builtin table[] = {
    {"abs",   1, Builtin::Shape::IntOrReal, "fabs", "shm_fn_abs_int"},
    {"sqrt",  1, Builtin::Shape::Real,      "sqrt",     nullptr},
    {"log",   1, Builtin::Shape::Real,      "log",      nullptr},
    {"exp",   1, Builtin::Shape::Real,      "exp",      nullptr},
    {"hypot", 2, Builtin::Shape::Real,      "hypot",    nullptr},
    {"sin",   1, Builtin::Shape::Real,      "sin",      nullptr},
    {"cos",   1, Builtin::Shape::Real,      "cos",      nullptr},
    {"tan",   1, Builtin::Shape::Real,      "tan",      nullptr},
    {"asin",  1, Builtin::Shape::Real,      "asin",     nullptr},
    {"acos",  1, Builtin::Shape::Real,      "acos",     nullptr},
    {"atan",  1, Builtin::Shape::Real,      "atan",     nullptr},
    {"atan2", 2, Builtin::Shape::Real,      "atan2",    nullptr},
    {"pow",   2, Builtin::Shape::Real,      "pow",      nullptr},
    {"round", 1, Builtin::Shape::Real,      "round",    nullptr},
    {"ceil",  1, Builtin::Shape::Real,      "ceil",     nullptr},
    {"floor", 1, Builtin::Shape::Real,      "floor",    nullptr},

    {"trunc", 1, Builtin::Shape::Real,      "trunc",    nullptr},

    // Added 2026-08-26, and each one is a row and nothing else: the symbol is
    // libm's, so there is no wrapper to write and the archive does not grow.
    // C99 rather than C89 - log2, cbrt and the hyperbolics postdate the
    // standard Compiler-C targets - but this is a call into the platform's
    // libm, not a C program, and all three of ours have them.
    {"fmod",  2, Builtin::Shape::Real,      "fmod",     nullptr},
    {"sinh",  1, Builtin::Shape::Real,      "sinh",     nullptr},
    {"cosh",  1, Builtin::Shape::Real,      "cosh",     nullptr},
    {"tanh",  1, Builtin::Shape::Real,      "tanh",     nullptr},
    {"log10", 1, Builtin::Shape::Real,      "log10",    nullptr},
    {"log2",  1, Builtin::Shape::Real,      "log2",     nullptr},
    {"cbrt",  1, Builtin::Shape::Real,      "cbrt",     nullptr},
    {"max",   2, Builtin::Shape::IntOrReal, "shm_fn_max_real", "shm_fn_max_int"},
    {"min",   2, Builtin::Shape::IntOrReal, "shm_fn_min_real", "shm_fn_min_int"},

    {"len",   1, Builtin::Shape::Length,    nullptr,           nullptr}
};

const int count = static_cast<int>(sizeof table / sizeof table[0]);

}

// Names a person will reasonably try, and the reason each one cannot be
// borrowed. Not a blocklist: every entry here is refused because its C
// signature needs a type Shalimar does not have, and saying which type is the
// difference between an answer and a refusal. docs/FOREIGN.md explains why
// the boundary falls exactly here.
//
// The list is short on purpose. It exists to turn the most likely mistakes
// into instructions; anything not on it still gets a plain "not a library
// function this compiler knows", which is true and not misleading.
namespace {
struct Unborrowable { const char *name; const char *why; };
const Unborrowable kUnborrowable[] = {
    {"memset",  "takes a pointer, which Shalimar has no type for"},
    {"memcpy",  "takes a pointer, which Shalimar has no type for"},
    {"strlen",  "takes a pointer, which Shalimar has no type for"},
    {"strcpy",  "takes a pointer, which Shalimar has no type for"},
    {"strcmp",  "takes a pointer, which Shalimar has no type for"},
    {"malloc",  "returns a pointer, which Shalimar has no type for"},
    {"free",    "takes a pointer, which Shalimar has no type for"},
    {"fopen",   "returns a pointer, which Shalimar has no type for"},
    {"qsort",   "takes a function pointer, which Shalimar has no type for"},
    {"printf",  "takes a variable number of arguments, which Shalimar has no form for"},
    {"scanf",   "takes a variable number of arguments, which Shalimar has no form for"},
    {"modf",    "writes through a pointer; a two-output 'fun' is the Shalimar shape for it"},
    {"frexp",   "writes through a pointer; a two-output 'fun' is the Shalimar shape for it"},
};
}

const char *whyNotBorrowable(const std::string &name) {
    for (std::size_t i = 0; i < sizeof kUnborrowable / sizeof kUnborrowable[0]; ++i)
        if (name == kUnborrowable[i].name) return kUnborrowable[i].why;
    return nullptr;
}

int findBuiltin(const std::string &name) {
    for (int i = 0; i < count; ++i) {
        if (name == table[i].name) return i;
    }
    return -1;
}

const Builtin &builtin(int index) { return table[index]; }
int builtinCount() { return count; }

bool isConstant(const std::string &name) { return name == "pi" || name == "e"; }

double constantValue(const std::string &name) {
    return name == "pi" ? 3.141592653589793 : 2.718281828459045;
}

}
