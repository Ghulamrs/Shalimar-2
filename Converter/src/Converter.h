#ifndef C2S_CONVERTER_H
#define C2S_CONVERTER_H

#include <string>
#include <vector>

#include "Diagnostics.h"
#include "Options.h"

namespace c2s {

class Converter {
public:
    struct Result {

        bool ok = false;

        std::string output;

        int beyondCount = 0;

        std::vector<Diagnostic> diagnostics;

        std::vector<std::string> questions;

        std::string summary;
    };

    static Result convert(const std::string &sourceText, const std::string &name,
                          Direction direction,
                          const Permissions &permissions = Permissions(),
                          bool emitIncludes = true);

    static Result convertFile(const std::string &sourceText, const std::string &name,
                              const Permissions &permissions = Permissions());

    static Result canonicalise(const std::string &sourceText, const std::string &name,
                               Direction direction);
};

}

#endif
