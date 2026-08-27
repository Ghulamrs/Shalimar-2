#include "Options.h"

#include "Diagnostics.h"

namespace c2s {

namespace {

bool endsWith(const std::string &text, const std::string &suffix) {
    if (text.size() < suffix.size()) return false;
    return text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}

const char *Options::usage() {
    return
        "usage: c2s [options] <input>\n"
        "\n"
        "  Converts C89 to Shalimar and Shalimar to C89. The direction is taken\n"
        "  from the input's extension - .c becomes .shm, .shm and .shl become .c -\n"
        "  unless one of --to-shalimar or --to-c says otherwise.\n"
        "\n"
        "  -o <file>            write here rather than to standard output\n"
        "  -I <dir>             a directory the C preprocessor scan may look in\n"
        "  --to-shalimar        convert the input as C89\n"
        "  --to-c               convert the input as Shalimar\n"
        "  --no-includes        omit the #include lines generated C needs\n"
        "  --canon              print the input back in canonical form,\n"
        "                       converting nothing\n"
        "\n"
        "  Rewrites that are refused by default, because each one compiles\n"
        "  without meaning quite what the original did:\n"
        "\n"
        "  --allow-short-circuit   '&&' and '||' as nested ifs\n"
        "  --allow-char-arithmetic int() around a char reaching an operator\n"
        "  --allow-narrowing       long, unsigned and float narrowed to int/real\n"
        "  --pragmatic             all three of the above\n"
        "\n"
        "  --list-codes         print the diagnostic catalogue and stop\n"
        "  --version            print the version and stop\n"
        "  -h, --help           print this and stop\n";
}

bool Options::parse(int argc, char **argv, Diagnostics &diagnostics) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            showHelp_ = true;
            return true;
        }
        if (arg == "--version") {
            showVersion_ = true;
            return true;
        }
        if (arg == "--list-codes") {
            listCodes_ = true;
            return true;
        }

        if (arg == "-o") {
            if (i + 1 >= argc) {
                diagnostics.report(Severity::SyntaxError, "C0002", "-o needs a file name");
                return false;
            }
            output_ = argv[++i];
            continue;
        }
        if (arg == "-I") {
            if (i + 1 >= argc) {
                diagnostics.report(Severity::SyntaxError, "C0002", "-I needs a directory");
                return false;
            }
            includePath_.push_back(argv[++i]);
            continue;
        }
        if (arg.size() > 2 && arg.compare(0, 2, "-I") == 0) {
            includePath_.push_back(arg.substr(2));
            continue;
        }

        if (arg == "--to-shalimar") { direction_ = Direction::CToShalimar; continue; }
        if (arg == "--to-c")        { direction_ = Direction::ShalimarToC; continue; }
        if (arg == "--no-includes") { emitIncludes_ = false; continue; }
        if (arg == "--canon")        { canonicalise_ = true; continue; }

        if (arg == "--allow-short-circuit")   { permissions_.allowShortCircuit(); continue; }
        if (arg == "--allow-char-arithmetic") { permissions_.allowCharArithmetic(); continue; }
        if (arg == "--allow-narrowing")       { permissions_.allowNarrowing(); continue; }
        if (arg == "--pragmatic")             { permissions_.allowEverything(); continue; }

        if (!arg.empty() && arg[0] == '-' && arg != "-") {
            diagnostics.report(Severity::SyntaxError, "C0001", "unknown option '" + arg + "'");
            return false;
        }

        if (!input_.empty()) {
            diagnostics.report(Severity::SyntaxError, "C0003",
                               "only one input file at a time, and '" + input_ +
                               "' was already named");
            return false;
        }
        input_ = arg;
    }

    if (input_.empty()) {
        diagnostics.report(Severity::SyntaxError, "C0004", "no input file");
        return false;
    }

    if (direction_ == Direction::Infer && resolvedDirection() == Direction::Infer) {
        diagnostics.report(Severity::SyntaxError, "C0005",
                           "cannot tell which way to convert '" + input_ +
                           "' - name it .c, .shm or .shl, or say --to-shalimar or --to-c");
        return false;
    }

    return true;
}

Direction Options::resolvedDirection() const {
    if (direction_ != Direction::Infer) return direction_;
    if (endsWith(input_, ".c")) return Direction::CToShalimar;
    if (endsWith(input_, ".shm") || endsWith(input_, ".shl")) return Direction::ShalimarToC;
    return Direction::Infer;
}

}
