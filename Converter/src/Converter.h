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

        // The input line each line of `output` came from, indexed by output
        // line minus one, and 0 where no construct owns it.
        //
        // **Filled for C -> Shalimar only**, because that is the direction with
        // a reader who cannot see the output: the app converts, hands the
        // Shalimar to its interpreter, and everything said from there names a
        // line of a file that is not on screen. This turns those numbers back
        // into lines of the C. `--canon` does not fill it, and neither does
        // Shalimar -> C, where the C is what the author is given.
        std::vector<int> lineMap;

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
