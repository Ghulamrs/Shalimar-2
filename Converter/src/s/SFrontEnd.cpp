#include "SFrontEnd.h"

#include "../Diagnostics.h"
#include "../Source.h"
#include "vendor/Check.h"
#include "vendor/Parser.h"
#include "vendor/Token.h"

namespace c2s {

void SFrontEnd::carryOver(const shalimar::Diagnostics &from, const Source &source,
                          Diagnostics &to) const {
    const std::vector<shalimar::Message> &messages = from.messages();
    for (std::size_t i = 0; i < messages.size(); ++i) {
        const shalimar::Message &message = messages[i];
        const Severity severity = message.severity == shalimar::Severity::Error
                                      ? Severity::SyntaxError
                                      : Severity::Warning;
        to.report(severity, source, Location(source.name(), message.line, 0),
                  "S1000", message.text);
    }
}

std::unique_ptr<shalimar::Program> SFrontEnd::parse(const Source &source,
                                                    Diagnostics &diagnostics) {
    shalimar::LexResult lexed = shalimar::tokenize(source.text());
    if (lexed.failed) {
        diagnostics.report(Severity::SyntaxError, source,
                           Location(source.name(), lexed.errorLine, 0), "S1001",
                           lexed.error);
        return nullptr;
    }

    shalimar::Diagnostics collected;
    shalimar::Parser parser(lexed.tokens, collected, 0);
    std::unique_ptr<shalimar::Program> program = parser.parse();

    carryOver(collected, source, diagnostics);
    if (collected.hasErrors()) return nullptr;
    return program;
}

std::unique_ptr<shalimar::Program> SFrontEnd::parseAndCheck(const Source &source,
                                                            Diagnostics &diagnostics) {
    shalimar::LexResult lexed = shalimar::tokenize(source.text());
    if (lexed.failed) {
        diagnostics.report(Severity::SyntaxError, source,
                           Location(source.name(), lexed.errorLine, 0), "S1001",
                           lexed.error);
        return nullptr;
    }

    shalimar::Diagnostics collected;
    shalimar::Parser parser(lexed.tokens, collected, 0);
    std::unique_ptr<shalimar::Program> program = parser.parse();

    if (program != nullptr && !collected.hasErrors()) {
        shalimar::Checker checker(collected);
        checker.check(*program);
    }

    carryOver(collected, source, diagnostics);
    if (program == nullptr || collected.hasErrors()) return nullptr;
    return program;
}

}
