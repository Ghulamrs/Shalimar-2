#ifndef C2S_OPTIONS_H
#define C2S_OPTIONS_H

#include <string>
#include <vector>

namespace c2s {

class Diagnostics;

enum class Direction { Infer, CToShalimar, ShalimarToC };

class Permissions {
public:
    Permissions()
        : shortCircuit_(false), charArithmetic_(false), narrowing_(false) {}

    bool shortCircuit() const { return shortCircuit_; }
    void allowShortCircuit() { shortCircuit_ = true; }


    bool charArithmetic() const { return charArithmetic_; }
    void allowCharArithmetic() { charArithmetic_ = true; }

    bool narrowing() const { return narrowing_; }
    void allowNarrowing() { narrowing_ = true; }

    void allowEverything() {
        shortCircuit_ = true;
        charArithmetic_ = true;
        narrowing_ = true;
    }

private:
    bool shortCircuit_;
    bool charArithmetic_;
    bool narrowing_;
};

class Options {
public:
    Options() : direction_(Direction::Infer), emitIncludes_(true),
                canonicalise_(false), showHelp_(false), showVersion_(false),
                listCodes_(false) {}

    bool parse(int argc, char **argv, Diagnostics &diagnostics);

    const std::string &input() const { return input_; }
    const std::string &output() const { return output_; }
    Direction direction() const { return direction_; }
    const std::vector<std::string> &includePath() const { return includePath_; }
    const Permissions &permissions() const { return permissions_; }

    bool emitIncludes() const { return emitIncludes_; }

    bool canonicalise() const { return canonicalise_; }

    bool showHelp() const { return showHelp_; }
    bool showVersion() const { return showVersion_; }
    bool listCodes() const { return listCodes_; }

    Direction resolvedDirection() const;

    static const char *usage();

private:
    std::string input_;
    std::string output_;
    Direction direction_;
    std::vector<std::string> includePath_;
    Permissions permissions_;
    bool emitIncludes_;
    bool canonicalise_;
    bool showHelp_;
    bool showVersion_;
    bool listCodes_;
};

}

#endif
