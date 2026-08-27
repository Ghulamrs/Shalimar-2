#ifndef C2S_S_SFRONTEND_H
#define C2S_S_SFRONTEND_H

#include <memory>
#include <string>

#include "vendor/Ast.h"
#include "vendor/Diag.h"

namespace c2s {

class Diagnostics;
class Source;

class SFrontEnd {
public:

    std::unique_ptr<shalimar::Program> parse(const Source &source,
                                             Diagnostics &diagnostics);

    std::unique_ptr<shalimar::Program> parseAndCheck(const Source &source,
                                                     Diagnostics &diagnostics);

private:
    void carryOver(const shalimar::Diagnostics &from, const Source &source,
                   Diagnostics &to) const;
};

}

#endif
