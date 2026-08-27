#ifndef C2S_C_CMACRO_H
#define C2S_C_CMACRO_H

#include <vector>

#include "CPreScan.h"
#include "CToken.h"

namespace c2s {

class Source;
class Diagnostics;

bool expandMacros(const std::vector<CPreScan::Macro> &macros,
                  std::vector<CToken> &tokens,
                  const Source &source, Diagnostics &diagnostics);

}

#endif
