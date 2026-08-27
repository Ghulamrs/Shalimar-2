#include "Parser.h"

namespace shalimar {

Parser::Parser(const std::vector<Token> &tokens, Diagnostics &diagnostics, int unit)
    : tokens_(tokens), diag_(diagnostics), unit_(unit) {}

const Token &Parser::peek(size_t ahead) const {
    static const Token endOfInput;
    size_t k = index_ + ahead;
    return k < tokens_.size() ? tokens_[k] : endOfInput;
}

bool Parser::atOperator(const char *spelling) const {
    return current().kind == Tok::Operator && current().text == spelling;
}

const Token &Parser::advance() {
    const Token &t = current();
    if (index_ < tokens_.size()) ++index_;
    return t;
}

bool Parser::match(Tok kind) {
    if (!at(kind)) return false;
    advance();
    return true;
}

int Parser::lastLine() const {
    return tokens_.empty() ? 1 : tokens_.back().line;
}

std::string Parser::unexpected() const {
    if (current().kind == Tok::EndOfInput) return "Program ends unfinished";
    return "Unexpected '" + spellingOf(current()) + "'";
}

void Parser::fail(const std::string &text) {
    fail(current().line, text);
}

void Parser::fail(int line, const std::string &text) {
    if (failed_) return;
    diag_.error(unit_, line > 0 ? line : lastLine(), text);
    failed_ = true;
}

bool Parser::expect(Tok kind, const std::string &text) {
    if (match(kind)) return true;
    fail(text);
    return false;
}

bool Parser::startsLine(size_t at) const {
    if (at == 0) return true;
    if (at >= tokens_.size()) return false;
    return tokens_[at].line != tokens_[at - 1].line;
}

std::unique_ptr<Program> Parser::parse() {
    std::unique_ptr<Program> program(new Program());

    while (index_ < tokens_.size() && !failed_) {
        if (at(Tok::Fun)) {
            std::unique_ptr<Function> f = parseFunction();
            if (failed_ || !f) return nullptr;
            program->add(std::move(f));
            continue;
        }
        if (at(Tok::Uses)) {
            if (!parseUses(*program)) return nullptr;
            continue;
        }
        if (atDeclaration()) {
            StmtPtr declaration = parseDeclaration();
            if (failed_ || !declaration) return nullptr;
            program->addGlobal(std::move(declaration));
            continue;
        }
        if (current().kind == Tok::EndOfInput) { fail("Program ends unfinished"); return nullptr; }
        fail("'" + spellingOf(current()) + "' must be inside a function");
        return nullptr;
    }
    return failed_ ? nullptr : std::move(program);
}

// `uses sin, cos, tan` - what this file borrows from the C library.
//
// Global space only, and per file: see docs/FOREIGN.md. The names are taken
// here and checked later, because whether a name is borrowable is a question
// about the table rather than about the grammar, and a parser that answered it
// would have to carry the table.
//
// The comma is required between names and forbidden after the last one, which
// is the same rule the parameter list follows, so that `uses sin,` reads as a
// mistake rather than as a name that has not been typed yet.
bool Parser::parseUses(Program &program) {
    const int line = current().line;
    advance();

    // Two forms, told apart by one token. `uses sin, cos` borrows from the
    // table this compiler carries; `uses <real> = mean(a[]: real)` declares a
    // function the LINK will provide, and carries its own prototype because
    // nothing here can go and look one up. `<` cannot start a name, so the
    // choice needs no lookahead beyond the token in hand.
    if (atOperator("<")) {
        Prototype proto;
        proto.line = line;
        proto.isForeign = true;
        if (!parsePrototype(proto)) return false;
        program.declareForeign(std::move(proto));
        return true;
    }

    if (!at(Tok::Identifier)) {
        fail(line, "'uses' needs a library function's name, or a declaration");
        return false;
    }

    for (;;) {
        if (!at(Tok::Identifier)) { failUnexpected(); return false; }
        program.borrow(current().text, current().line);
        advance();
        if (!at(Tok::Comma)) break;
        // The comma's own line, not the next token's. A trailing comma is a
        // mistake made here, and reporting it where the parser happened to
        // stop would point at whatever innocent line follows.
        const int comma = current().line;
        advance();
        if (!at(Tok::Identifier)) {
            fail(comma, "a library function's name must follow the comma");
            return false;
        }
    }
    return true;
}

// The head of a function: `<outputs> = name(params)`. Shared by `fun`, which
// follows it with a body, and by the `uses` form, which does not - a foreign
// declaration IS a function head with nothing after it, and writing the parse
// twice is how the two would drift apart.
bool Parser::parsePrototype(Prototype &proto) {
    if (!atOperator("<")) { failUnexpected(); return false; }
    advance();

    while (!atOperator(">") && !atOperator(">=")) {
        const Type *type = scalarTypeHere();
        if (!type) { failUnexpected(); return false; }
        proto.outputs.push_back(type);
        if (!match(Tok::Comma)) break;
    }

    if (atOperator(">=")) {
        advance();
    } else {
        if (!atOperator(">")) { failUnexpected(); return false; }
        advance();
        if (!atOperator("=")) { failUnexpected(); return false; }
        advance();
    }

    if (!at(Tok::Identifier)) { failUnexpected(); return false; }
    proto.name = advance().text;

    if (!expect(Tok::ParensOpen, unexpected())) return false;
    while (!at(Tok::ParensClose)) {
        Param parameter;
        if (atOperator("&")) { advance(); parameter.byReference = true; }
        if (!at(Tok::Identifier)) { failUnexpected(); return false; }
        parameter.name = advance().text;

        int rank = 0;
        while (at(Tok::BracketOpen)) {
            advance();
            if (!expect(Tok::BracketClose, unexpected())) return false;
            ++rank;
        }
        if (!expect(Tok::Assign, unexpected())) return false;
        const Type *scalar = scalarTypeHere();
        if (!scalar) { failUnexpected(); return false; }

        const Type *type = scalar;
        for (int i = 0; i < rank; ++i) type = Type::arrayOf(type);
        parameter.type = type;

        if (rank > 0) parameter.byReference = false;
        proto.inputs.push_back(parameter);
        if (!match(Tok::Comma)) break;
    }
    if (!expect(Tok::ParensClose, unexpected())) return false;

    proto.unit = unit_;
    return true;
}

std::unique_ptr<Function> Parser::parseFunction() {
    Prototype proto;
    proto.line = current().line;
    advance();

    if (!parsePrototype(proto)) return nullptr;

    Block body = parseBlock();
    if (failed_) return nullptr;

    return std::unique_ptr<Function>(new Function(std::move(proto), std::move(body)));
}

Block Parser::parseBlock() {
    Block body;
    if (!expect(Tok::BraceOpen, "Missing '{' to start block")) return body;

    while (index_ < tokens_.size() && !at(Tok::BraceClose)) {
        StmtPtr s = parseStatement();
        if (failed_) return body;
        if (s) body.push_back(std::move(s));
    }
    if (!expect(Tok::BraceClose, "Missing '}' to close block")) return body;
    return body;
}

StmtPtr Parser::parseStatement() {
    StmtPtr made = parseStatementBody();
    if (made) made->setUnit(unit_);
    return made;
}

StmtPtr Parser::parseStatementBody() {
    if (at(Tok::PrintLine) || at(Tok::PrintInline)) return parsePrint();
    // A declaration goes wherever a statement goes - inside an `if`, inside a loop,
    // halfway down a function after the work has started. It was refused below the
    // top of a function body once; the rule went so that a C program keeps its shape
    // when it is converted rather than having every local hoisted to the top.
    //
    // What did NOT go is the lifetime. A declared local is still the whole call's, so
    // Checker refuses a second declaration of the same name anywhere in the function
    // and ends the name's visibility with its block. §6 of the specification.
    if (atDeclaration()) return parseDeclaration();
    if (at(Tok::If))    return parseIf();
    if (at(Tok::While)) return parseWhile();
    if (at(Tok::For))   return parseFor();

    if (at(Tok::Break) || at(Tok::Continue)) {
        const bool isBreak = at(Tok::Break);
        const int line = advance().line;
        if (loopDepth_ == 0) {
            fail(line, std::string("'") + (isBreak ? "break" : "continue") +
                       "' outside a loop");
            return nullptr;
        }
        if (isBreak) return StmtPtr(new Break(line));
        return StmtPtr(new Continue(line));
    }

    if (at(Tok::Return)) return parseReturn();
    if (atOperator("<") && looksLikeMultiAssignHeader(index_)) return parseMultiAssign();

    if (at(Tok::Identifier)) {
        Tok next = peek(1).kind;

        if (next == Tok::BracketOpen) {
            size_t i = index_ + 1;
            int depth = 0;
            while (i < tokens_.size()) {
                if (tokens_[i].kind == Tok::BracketOpen) ++depth;
                else if (tokens_[i].kind == Tok::BracketClose) { if (--depth == 0) { ++i; break; } }
                ++i;
            }
            while (i < tokens_.size() && tokens_[i].kind == Tok::BracketOpen) {
                depth = 0;
                while (i < tokens_.size()) {
                    if (tokens_[i].kind == Tok::BracketOpen) ++depth;
                    else if (tokens_[i].kind == Tok::BracketClose) { if (--depth == 0) { ++i; break; } }
                    ++i;
                }
            }
            next = i < tokens_.size() ? tokens_[i].kind : Tok::EndOfInput;
            if (next == Tok::Assign || next == Tok::PlusAssign || next == Tok::MinusAssign ||
                (next == Tok::Operator && tokens_[i].text == "=")) {
                return parseAssignment();
            }
            next = peek(1).kind;
        }
        if (next == Tok::Assign || next == Tok::PlusAssign || next == Tok::MinusAssign ||
            (next == Tok::Operator && peek(1).text == "=")) {
            return parseAssignment();
        }

        if (next == Tok::ParensOpen) {
            const int line = current().line;
            ExprPtr call = parsePrimary();
            if (failed_) return nullptr;
            return StmtPtr(new CallStmt(std::move(call), line));
        }
    }
    failUnexpected();
    return nullptr;
}

bool Parser::atDeclaration() const {
    if (!at(Tok::Int) && !at(Tok::Real) && !at(Tok::Char)) return false;
    return peek(1).kind != Tok::ParensOpen;
}

const Type *Parser::scalarTypeHere() {
    if (match(Tok::Int))  return Type::intType();
    if (match(Tok::Real)) return Type::realType();
    if (match(Tok::Char)) return Type::charType();
    return nullptr;
}

StmtPtr Parser::parseDeclaration() {
    const int line = current().line;
    const Type *type = scalarTypeHere();

    if (!at(Tok::Identifier)) { failUnexpected(); return nullptr; }
    const std::string name = advance().text;

    std::vector<ExprPtr> extents;
    while (at(Tok::BracketOpen)) {
        advance();
        ExprPtr extent = parseExpression();
        if (failed_) return nullptr;
        if (!expect(Tok::BracketClose, unexpected())) return nullptr;
        extents.push_back(std::move(extent));
    }

    ExprPtr initial;
    if (match(Tok::Assign)) {
        initial = parseInitializer();
        if (failed_) return nullptr;
    }
    std::unique_ptr<Declare> node(new Declare(type, name, std::move(initial), line));
    for (ExprPtr &extent : extents) node->addExtent(std::move(extent));
    return StmtPtr(node.release());
}

StmtPtr Parser::parseAssignment() {
    const int line = current().line;
    const std::string name = advance().text;

    ExprPtr target(new Var(name));
    while (at(Tok::BracketOpen)) {
        advance();
        ExprPtr index = parseExpression();
        if (failed_) return nullptr;
        if (!expect(Tok::BracketClose, unexpected())) return nullptr;
        target.reset(new Index(std::move(target), std::move(index)));
    }

    const Tok how = advance().kind;
    ExprPtr value = parseInitializer();
    if (failed_) return nullptr;

    if (how == Tok::PlusAssign || how == Tok::MinusAssign) {
        return StmtPtr(new CompoundAssign(std::move(target), how == Tok::PlusAssign,
                                          std::move(value), line));
    }
    return StmtPtr(new Assign(std::move(target), std::move(value), line));
}

ExprPtr Parser::parseInitializer() {
    if (at(Tok::BraceOpen)) return parseArrayLiteral();
    return parseExpression();
}

ExprPtr Parser::parseArrayLiteral() {
    advance();
    std::unique_ptr<ArrayLit> node(new ArrayLit());
    if (match(Tok::BraceClose)) return ExprPtr(node.release());

    while (true) {
        if (at(Tok::Comma) || at(Tok::BraceClose)) {
            node->add(ExprPtr(new Blank()));
        } else {
            ExprPtr slot = parseInitializer();
            if (failed_) return nullptr;
            node->add(std::move(slot));
        }
        if (match(Tok::Comma)) continue;
        break;
    }
    if (!expect(Tok::BraceClose, unexpected())) return nullptr;
    return ExprPtr(node.release());
}

StmtPtr Parser::parseReturn() {
    const int line = current().line;
    advance();

    std::unique_ptr<Return> node(new Return(line));
    if (!startsTerm() || startsLine(index_) || looksLikeNewStatement(index_)) {
        return StmtPtr(node.release());
    }

    if (at(Tok::ParensOpen) && parenGroupHasTopLevelComma(index_)) {
        advance();
        while (true) {
            ExprPtr value = parseExpression();
            if (failed_) return nullptr;
            node->add(std::move(value));
            if (!match(Tok::Comma)) break;
        }
        if (!expect(Tok::ParensClose, unexpected())) return nullptr;
        return StmtPtr(node.release());
    }

    ExprPtr value = parseExpression();
    if (failed_) return nullptr;
    node->add(std::move(value));
    return StmtPtr(node.release());
}

bool Parser::parenGroupHasTopLevelComma(size_t start) const {
    int depth = 0;
    for (size_t i = start; i < tokens_.size(); ++i) {
        switch (tokens_[i].kind) {
        case Tok::ParensOpen:
        case Tok::BracketOpen:
            ++depth;
            break;
        case Tok::ParensClose:
        case Tok::BracketClose:
            if (--depth == 0) return false;
            break;
        case Tok::Comma:
            if (depth == 1) return true;
            break;
        default:
            break;
        }
    }
    return false;
}

bool Parser::looksLikeMultiAssignHeader(size_t start) const {
    size_t i = start + 1;
    if (i >= tokens_.size() || tokens_[i].kind != Tok::Identifier) return false;
    ++i;
    while (i < tokens_.size() && tokens_[i].kind == Tok::Comma) {
        ++i;
        if (i >= tokens_.size() || tokens_[i].kind != Tok::Identifier) return false;
        ++i;
    }
    if (i >= tokens_.size()) return false;
    const bool closes = tokens_[i].kind == Tok::Operator && tokens_[i].text == ">";
    if (!closes) return false;
    ++i;
    return i < tokens_.size() && tokens_[i].kind == Tok::Assign;
}

StmtPtr Parser::parseMultiAssign() {
    const int line = current().line;
    advance();

    std::unique_ptr<MultiAssign> node(new MultiAssign(line));
    node->addTarget(advance().text);
    while (match(Tok::Comma)) node->addTarget(advance().text);
    advance();
    advance();

    if (!at(Tok::Identifier) || peek(1).kind != Tok::ParensOpen) {
        failUnexpected();
        return nullptr;
    }
    ExprPtr call = parsePrimary();
    if (failed_) return nullptr;
    node->setCall(std::move(call));
    return StmtPtr(node.release());
}

StmtPtr Parser::parseIf() {
    const int line = current().line;
    advance();

    std::unique_ptr<If> node(new If(line));
    ExprPtr condition = parseExpression();
    if (failed_) return nullptr;
    Block body = parseBlock();
    if (failed_) return nullptr;
    node->addBranch(std::move(condition), std::move(body));

    while (atElseIf()) {
        // `else` then `if`. Two tokens, taken together, which is what keeps a
        // bare `else` falling through to the branch below.
        advance();
        advance();
        ExprPtr next = parseExpression();
        if (failed_) return nullptr;
        Block branch = parseBlock();
        if (failed_) return nullptr;
        node->addBranch(std::move(next), std::move(branch));
    }
    if (match(Tok::Else)) {
        Block otherwise = parseBlock();
        if (failed_) return nullptr;
        node->setElse(std::move(otherwise));
    }
    return StmtPtr(node.release());
}

StmtPtr Parser::parseWhile() {
    const int line = current().line;
    advance();
    ExprPtr condition = parseExpression();
    if (failed_) return nullptr;
    ++loopDepth_;
    Block body = parseBlock();
    --loopDepth_;
    if (failed_) return nullptr;
    return StmtPtr(new While(std::move(condition), std::move(body), line));
}

StmtPtr Parser::parseFor() {
    const int line = current().line;
    advance();

    if (!at(Tok::Identifier)) { failUnexpected(); return nullptr; }
    const std::string name = advance().text;

    ExprPtr start;
    ExprPtr end;
    if (atOperator("<")) {
        advance();
        ExprPtr count = parseExpression();
        if (failed_) return nullptr;
        start.reset(new IntLit(0));
        end.reset(new Binary(Binary::Op::Subtract, std::move(count), ExprPtr(new IntLit(1))));
    } else {
        if (!expect(Tok::Assign, unexpected())) return nullptr;
        start = parseExpression();
        if (failed_) return nullptr;
        if (!expect(Tok::To, unexpected())) return nullptr;
        end = parseExpression();
        if (failed_) return nullptr;
    }

    ExprPtr step;
    if (match(Tok::Step)) {
        step = parseExpression();
        if (failed_) return nullptr;
    }

    ++loopDepth_;
    Block body = parseBlock();
    --loopDepth_;
    if (failed_) return nullptr;

    return StmtPtr(new For(name, std::move(start), std::move(end),
                           std::move(step), std::move(body), line));
}

StmtPtr Parser::parsePrint() {
    if (!startsLine(index_)) {
        fail(std::string(at(Tok::PrintLine) ? "'?'" : "'?\?'") + " must start its line");
        return nullptr;
    }
    const bool newline = at(Tok::PrintLine);
    const int line = current().line;
    advance();

    std::unique_ptr<Print> node(new Print(newline, line));
    while (startsTerm() && !startsLine(index_) && !looksLikeNewStatement(index_)) {
        ExprPtr item;
        if (atPrecisionDirective()) {
            advance();
            advance();
            ExprPtr places = parseExpression();
            if (failed_) return nullptr;
            if (!expect(Tok::ParensClose, unexpected())) return nullptr;
            item.reset(new Precision(std::move(places)));
        } else {
            item = parseExpression();
        }
        if (failed_) return nullptr;
        node->add(std::move(item));
    }
    return StmtPtr(node.release());
}

bool Parser::startsTerm() const {
    switch (current().kind) {
    case Tok::IntLiteral:
    case Tok::RealLiteral:
    case Tok::StringLiteral:
    case Tok::Identifier:
    case Tok::ParensOpen:
        return true;
    case Tok::Operator:

        return current().text == "-";
    case Tok::Int:
    case Tok::Real:
    case Tok::Char:

        return peek(1).kind == Tok::ParensOpen;
    default:
        return false;
    }
}

bool Parser::looksLikeNewStatement(size_t at) const {
    if (at + 1 >= tokens_.size()) return false;
    if (tokens_[at].kind != Tok::Identifier) return false;
    const Token &next = tokens_[at + 1];
    switch (next.kind) {
    case Tok::Assign:
    case Tok::PlusAssign:
    case Tok::MinusAssign:
        return true;
    case Tok::Operator:
        return next.text == "=";
    default:
        return false;
    }
}

ExprPtr Parser::parseExpression() {
    return parseOr();
}

ExprPtr Parser::parseOr() {
    ExprPtr left = parseAnd();
    while (!failed_ && atOperator("|")) {
        advance();
        ExprPtr right = parseAnd();
        if (failed_) return nullptr;
        left.reset(new Binary(Binary::Op::Or, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parseAnd() {
    ExprPtr left = parseComparison();
    while (!failed_ && atOperator("&")) {
        advance();
        ExprPtr right = parseComparison();
        if (failed_) return nullptr;
        left.reset(new Binary(Binary::Op::And, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parseComparison() {
    ExprPtr left = parseAdditive();
    while (!failed_) {
        Binary::Op op;

        if (atOperator("<") && looksLikeMultiAssignHeader(index_)) break;
        if      (atOperator("="))  op = Binary::Op::Equal;
        else if (atOperator("!=")) op = Binary::Op::NotEqual;
        else if (atOperator("<"))  op = Binary::Op::Less;
        else if (atOperator(">"))  op = Binary::Op::Greater;
        else if (atOperator("<=")) op = Binary::Op::LessEqual;
        else if (atOperator(">=")) op = Binary::Op::GreaterEqual;
        else break;
        advance();
        ExprPtr right = parseAdditive();
        if (failed_) return nullptr;
        left.reset(new Binary(op, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parseAdditive() {
    ExprPtr left = parseMultiplicative();
    while (!failed_ && (atOperator("+") || atOperator("-"))) {
        const Binary::Op op = atOperator("+") ? Binary::Op::Add : Binary::Op::Subtract;
        advance();
        ExprPtr right = parseMultiplicative();
        if (failed_) return nullptr;
        left.reset(new Binary(op, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parseMultiplicative() {
    ExprPtr left = parsePower();
    while (!failed_) {
        Binary::Op op;
        if      (atOperator("*")) op = Binary::Op::Multiply;
        else if (atOperator("/")) op = Binary::Op::Divide;
        else if (atOperator("%")) op = Binary::Op::Modulus;
        else break;
        advance();
        ExprPtr right = parsePower();
        if (failed_) return nullptr;
        left.reset(new Binary(op, std::move(left), std::move(right)));
    }
    return left;
}

ExprPtr Parser::parsePower() {
    ExprPtr base = parsePrimary();
    if (failed_ || !atOperator("^")) return base;
    advance();
    ExprPtr exponent = parsePower();
    if (failed_) return nullptr;
    return ExprPtr(new Binary(Binary::Op::Power, std::move(base), std::move(exponent)));
}

ExprPtr Parser::parsePrimary() {

    if (atOperator("-")) {
        advance();
        ExprPtr operand = parsePrimary();
        if (failed_) return nullptr;

        if (operand->isIntLiteral()) {
            const int32_t value = static_cast<const IntLit &>(*operand).value();
            return ExprPtr(new IntLit(-value));
        }
        if (operand->isRealLiteral()) {
            const double value = static_cast<const RealLit &>(*operand).value();
            return ExprPtr(new RealLit(-value));
        }
        return ExprPtr(new Binary(Binary::Op::Subtract,
                                  ExprPtr(new IntLit(0)), std::move(operand)));
    }
    if (at(Tok::IntLiteral)) {
        const Token &t = advance();
        return ExprPtr(new IntLit(t.intValue));
    }
    if (at(Tok::RealLiteral)) {
        const Token &t = advance();
        return ExprPtr(new RealLit(t.realValue));
    }
    if (at(Tok::StringLiteral)) {
        const Token &t = advance();
        return parsePostfix(ExprPtr(new StrLit(t.text)));
    }

    if ((at(Tok::Int) || at(Tok::Real) || at(Tok::Char)) && peek(1).kind == Tok::ParensOpen) {
        const Type *target = scalarTypeHere();
        advance();
        ExprPtr inner = parseExpression();
        if (failed_) return nullptr;
        if (!expect(Tok::ParensClose, unexpected())) return nullptr;
        return parsePostfix(ExprPtr(new Convert(std::move(inner), target)));
    }
    if (at(Tok::Identifier)) {
        const Token &t = advance();
        if (at(Tok::ParensOpen)) {
            advance();
            std::unique_ptr<Call> call(new Call(t.text, t.line));
            while (!at(Tok::ParensClose)) {
                ExprPtr argument = parseExpression();
                if (failed_) return nullptr;
                call->add(std::move(argument));
                if (!match(Tok::Comma)) break;
            }
            if (!expect(Tok::ParensClose, unexpected())) return nullptr;
            return parsePostfix(ExprPtr(call.release()));
        }
        return parsePostfix(ExprPtr(new Var(t.text)));
    }
    if (at(Tok::ParensOpen)) {
        advance();
        ExprPtr inner = parseExpression();
        if (failed_) return nullptr;
        if (!expect(Tok::ParensClose, unexpected())) return nullptr;
        return parsePostfix(std::move(inner));
    }
    failUnexpected();
    return nullptr;
}

ExprPtr Parser::parsePostfix(ExprPtr base) {
    while (!failed_) {
        if (at(Tok::BracketOpen)) {
            advance();
            ExprPtr index = parseExpression();
            if (failed_) return nullptr;
            if (!expect(Tok::BracketClose, unexpected())) return nullptr;
            base.reset(new Index(std::move(base), std::move(index)));
            continue;
        }
        if (at(Tok::Dot)) {
            advance();
            if (!at(Tok::Identifier)) { failUnexpected(); return nullptr; }
            const std::string what = advance().text;
            if (what == "row") {
                base.reset(new Dim(std::move(base), ExprPtr(new IntLit(0)), what));
            } else if (what == "col") {
                base.reset(new Dim(std::move(base), ExprPtr(new IntLit(1)), what));
            } else if (what == "dim") {
                if (!at(Tok::ParensOpen)) {
                    fail("'.dim' needs an axis, as .dim(0)");
                    return nullptr;
                }
                advance();
                ExprPtr axis = parseExpression();
                if (failed_) return nullptr;
                if (!expect(Tok::ParensClose, unexpected())) return nullptr;
                base.reset(new Dim(std::move(base), std::move(axis), what));
            } else {
                fail("No '." + what + "' - use .row, .col or .dim(n)");
                return nullptr;
            }

            if (at(Tok::Assign) || at(Tok::PlusAssign) || at(Tok::MinusAssign)) {
                fail("'." + what + "' is read-only");
                return nullptr;
            }
            continue;
        }
        break;
    }
    return base;
}

bool Parser::atPrecisionDirective() const {
    return at(Tok::Identifier) && current().text == "prec" && peek(1).kind == Tok::ParensOpen;
}

}
