import Foundation

extension Int {
    static prefix  func ++( x: inout Int) -> Int { x += 1; return x }
    static postfix func ++( x: inout Int) -> Int { x += 1; return (x - 1) }
}

enum ParseError: Error, CustomStringConvertible {
    case UnexpectedToken(String)
    case UnexpectedEndOfProgram
    case MissingOpeningBrace
    case MissingClosingBrace
    case PrintNotAtLineStart(String)
    case DeclarationInSubScope(String)
    case ReturnOutsideFunction
    case LoopControlOutsideLoop(String)
    case StatementInGlobalSpace(String)
    case UnknownAttribute(String)
    case AttributeNotAssignable(String)
    case DimNeedsAnAxis

    var message: String {
        switch self {
        case .UnexpectedToken(let tok):
            return "Unexpected '\(tok)'"
        case .UnexpectedEndOfProgram:
            return "Program ends unfinished"
        case .MissingOpeningBrace:
            return "Missing '{' to start block"
        case .MissingClosingBrace:
            return "Missing '}' to close block"
        case .PrintNotAtLineStart(let cmd):
            return "'\(cmd)' must start its line"
        case .DeclarationInSubScope(let name):
            return "'\(name)': declare it at the top of the function"
        case .ReturnOutsideFunction:
            return "'return' outside a function"
        case .LoopControlOutsideLoop(let word):
            return "'\(word)' outside a loop"
        case .StatementInGlobalSpace(let tok):
            return "'\(tok)' must be inside a function"
        case .UnknownAttribute(let name):
            return "No '.\(name)' - use .row, .col or .dim(n)"
        case .AttributeNotAssignable(let name):
            return "'.\(name)' is read-only"
        case .DimNeedsAnAxis:
            return "'.dim' needs an axis, as .dim(0)"
        }
    }

    var description: String { "Error: \(message)" }
}

struct LocatedParseError: Error, CustomStringConvertible {
    let error: ParseError
    let line: Int

    var description: String { "Error: line \(line): \(error.message)" }
}

class Parser {
    let tokens: [Token]
    var index = 0
    private(set) var parseError: Error?

    private var enclosingPrototype: PrototypeNode?

    private var blockDepth = 0

    // Counts the loops a statement is standing inside, which is the whole of what
    // 'break' and 'continue' need to be legal. Kept apart from blockDepth because
    // that one counts every sub-scope - an 'if' body raises it, and an 'if' is not
    // something either word can escape.
    private var loopDepth = 0

    init(tokens: [Token]) {
        self.tokens = tokens
    }

    var tokensAvailable: Bool {
        return index < tokens.count
    }

    func peekCurrentToken() -> Token {
        return tokensAvailable ? tokens[index]
                               : Token(kind: .EndOfInput, line: tokens.last?.line ?? 1)
    }

    func peekNextToken() -> Token {
        return index + 1 < tokens.count ? tokens[index + 1]
                                        : Token(kind: .EndOfInput, line: tokens.last?.line ?? 1)
    }

    func popCurrentToken() -> Token {
        let token = peekCurrentToken()
        if tokensAvailable { _ = index++ }
        return token
    }

    private func match(_ kind: TokenKind) -> Bool {
        if peekCurrentToken().kind == kind {
            _ = popCurrentToken()
            return true
        }
        return false
    }

    @discardableResult
    private func consume(_ expected: TokenKind) throws -> Token {
        let token = peekCurrentToken()
        guard token.kind == expected else {
            throw unexpected(token.kind)
        }
        return popCurrentToken()
    }

    func readIdentifier() throws -> String {
        guard case let TokenKind.Identifier(name) = peekCurrentToken().kind else {
            throw unexpected(peekCurrentToken().kind)
        }
        _ = popCurrentToken()
        return name
    }

    // Every diagnostic names what the programmer typed, never what the parser calls it.
    // Running off the end has no spelling at all, so it gets its own sentence rather than
    // a quoted stand-in.
    private func unexpected(_ kind: TokenKind) -> ParseError {
        if case .EndOfInput = kind { return .UnexpectedEndOfProgram }
        return .UnexpectedToken(describe(kind))
    }

    // Every token spells itself the way the programmer wrote it. Falling back to
    // "\(kind)" would print the Swift case name - a reader who typed '{' should
    // not have to learn that the parser calls it BraceOpen.
    private func describe(_ kind: TokenKind) -> String {
        switch kind {
        case .IntLiteral(let v):    return "\(v)"
        case .RealLiteral(let v):   return "\(v)"
        case .Identifier(let name): return name
        case .StringLiteral(let s): return "\"\(s)\""
        case .Operator(let op):     return op

        case .Assign:       return ":"
        case .PlusAssign:   return "+:"
        case .MinusAssign:  return "-:"
        case .PrintLine:    return "?"
        case .PrintInline:  return "??"
        case .ParensOpen:   return "("
        case .ParensClose:  return ")"
        case .BraceOpen:    return "{"
        case .BraceClose:   return "}"
        case .BracketOpen:  return "["
        case .BracketClose: return "]"
        case .Comma:        return ","
        case .Dot:          return "."

        case .If:     return "if"
        case .ElseIf: return "elseif"
        case .Else:   return "else"
        case .While:  return "while"
        case .For:    return "for"
        case .To:     return "to"
        case .Step:   return "step"
        case .Fun:    return "fun"
        case .Return: return "return"
        case .Break:    return "break"
        case .Continue: return "continue"
        case .Int:    return "int"
        case .Real:   return "real"
        case .Char:   return "char"

        case .EndOfInput: return "end of program"
        }
    }

    func parseProgram() -> [Node] {
        index = 0

        var nodes = [Node]()
        while tokensAvailable {
            do {
                switch peekCurrentToken().kind {
                case .Fun:
                    nodes.append(try parseDefinition())
                case .Int, .Real, .Char:
                    nodes.append(try parseDeclaration())
                default:
                    throw ParseError.StatementInGlobalSpace(describe(peekCurrentToken().kind))
                }
            } catch let error as ParseError {
                parseError = LocatedParseError(error: error, line: peekCurrentToken().line)
                break
            } catch {
                parseError = error
                break
            }
        }

        return nodes
    }

    private func isTypeKeyword(_ kind: TokenKind) -> Bool {
        switch kind {
        case .Int, .Real, .Char: return true
        default: return false
        }
    }

    private func parseScalarType() throws -> ShalimarType {
        switch peekCurrentToken().kind {
        case .Int:  _ = popCurrentToken(); return .int
        case .Real: _ = popCurrentToken(); return .real
        case .Char: _ = popCurrentToken(); return .char
        default: throw unexpected(peekCurrentToken().kind)
        }
    }

    private func wrap(_ scalar: ShalimarType, inArrays rank: Int) -> ShalimarType {
        var type = scalar
        for _ in 0..<rank { type = .array(type) }
        return type
    }

    func parseDeclaration() throws -> DeclareNode {
        let line = peekCurrentToken().line
        let scalar = try parseScalarType()
        let name = try readIdentifier()

        guard blockDepth == 0 else {
            throw ParseError.DeclarationInSubScope(name)
        }

        var sizes = [ExprNode]()
        while peekCurrentToken().kind == .BracketOpen {
            _ = popCurrentToken()
            sizes.append(try parseExpression())
            try consume(.BracketClose)
        }

        var initial: ExprNode? = nil
        if match(.Assign) {
            initial = try parseInitializer()
        }

        return DeclareNode(type: wrap(scalar, inArrays: sizes.count),
                           name: name,
                           sizes: sizes,
                           initial: initial,
                           line: line)
    }

    private func parseInitializer() throws -> ExprNode {
        guard peekCurrentToken().kind == .BraceOpen else {
            return try parseExpression()
        }
        _ = popCurrentToken()

        var elements = [ExprNode]()
        if match(.BraceClose) {
            return ArrayLiteralNode(elements: elements)
        }
        while true {
            // A slot with nothing in it is an omitted entry, not a syntax error: the commas
            // alone still fix the shape, so '{1.0,,}' is three slots and '{,,}' is three
            // empty ones. Checked here rather than in parseExpression because only a brace
            // literal has slots for something to be missing from.
            let kind = peekCurrentToken().kind
            if kind == .Comma || kind == .BraceClose {
                elements.append(BlankNode())
            } else {
                elements.append(try parseInitializer())
            }
            if match(.Comma) { continue }
            break
        }
        try consume(.BraceClose)
        return ArrayLiteralNode(elements: elements)
    }

    func parseDefinition() throws -> FunctionNode {
        let line = peekCurrentToken().line
        try consume(.Fun)

        // The output list is required, even when it is empty: every definition reads
        // 'fun <...> = name(...)'. It used to be optional, so 'fun = main()' parsed and
        // ran - a second spelling of the header that the grammar never described and
        // nothing else in the language would have recognised.
        // 'fun <>= main()' and 'fun <int>= f()' are legal, and were legal before '>='
        // became a token: written without the space the lexer now hands back a single
        // '>=', because it cannot see that this '>' closes a list rather than compares.
        // Accepting it here as the closing '>' plus the separator '=' keeps every header
        // that parsed before parsing now - the alternative was to make the space
        // mandatory, silently breaking programs for the sake of an operator they do
        // not use.
        var outputs = [ShalimarType]()
        var separatorTaken = false
        try consume(.Operator("<"))
        if match(.Operator(">=")) {
            separatorTaken = true
        } else if !match(.Operator(">")) {
            while true {
                outputs.append(try parseScalarType())
                if match(.Comma) { continue }
                break
            }
            if match(.Operator(">=")) {
                separatorTaken = true
            } else {
                try consume(.Operator(">"))
            }
        }
        if !separatorTaken { try consume(.Operator("=")) }

        let name = try readIdentifier()
        let inputs = try parseParensList(read: parseParameter)
        let prototype = PrototypeNode(name: name, outputs: outputs, inputs: inputs, line: line)

        let previousPrototype = enclosingPrototype
        let previousDepth = blockDepth
        let previousLoopDepth = loopDepth
        enclosingPrototype = prototype
        blockDepth = 0
        loopDepth = 0
        defer {
            enclosingPrototype = previousPrototype
            blockDepth = previousDepth
            loopDepth = previousLoopDepth
        }

        return FunctionNode(prototype: prototype, body: try parseBody())
    }

    func parseParameter() throws -> Parameter {
        let byReferenceMarker = match(.Operator("&"))
        let name = try readIdentifier()

        var rank = 0
        while peekCurrentToken().kind == .BracketOpen {
            _ = popCurrentToken()
            try consume(.BracketClose)
            rank += 1
        }

        try consume(.Assign)
        let type = wrap(try parseScalarType(), inArrays: rank)

        return Parameter(name: name, type: type, byReference: byReferenceMarker || type.isReference)
    }

    private func parseBody() throws -> [StmtNode] {
        guard peekCurrentToken().kind == .BraceOpen else {
            throw ParseError.MissingOpeningBrace
        }
        _ = popCurrentToken()

        var statements = [StmtNode]()
        while peekCurrentToken().kind != .BraceClose {
            guard tokensAvailable else { throw ParseError.MissingClosingBrace }
            statements.append(try parseStatement())
        }
        _ = popCurrentToken()
        return statements
    }

    private func parseSubScope() throws -> [StmtNode] {
        blockDepth += 1
        defer { blockDepth -= 1 }
        return try parseBody()
    }

    // A loop's own body, which is the only place 'break' and 'continue' mean anything.
    private func parseLoopBody() throws -> [StmtNode] {
        loopDepth += 1
        defer { loopDepth -= 1 }
        return try parseSubScope()
    }

    func parseStatement() throws -> StmtNode {
        let line = peekCurrentToken().line

        switch peekCurrentToken().kind {
        case .Int, .Real, .Char:
            return try parseDeclaration()

        case .Identifier:
            return try parseAssignmentOrCall(line: line)

        case .Operator("<"):
            guard looksLikeMultiAssignHeader(at: index) else {
                throw ParseError.UnexpectedToken("<")
            }
            let names = try parseAngleList(read: readIdentifier)
            try consume(.Assign)
            let callee = try readIdentifier()
            let call = try parseFunctionCall(callee: callee)
            return MultiAssignNode(targets: names.map { .variable($0) }, call: call, line: line)

        case .Return:
            return try parseReturn(line: line)

        // Checked before the token is taken, so the diagnostic points at the word
        // itself rather than at whatever follows it.
        case .Break:
            guard loopDepth > 0 else { throw ParseError.LoopControlOutsideLoop("break") }
            _ = popCurrentToken()
            return BreakNode(line: line)

        case .Continue:
            guard loopDepth > 0 else { throw ParseError.LoopControlOutsideLoop("continue") }
            _ = popCurrentToken()
            return ContinueNode(line: line)

        case .PrintLine:
            try requireLineStart("?")
            _ = popCurrentToken()
            return PrintNode(items: try parsePrintItems(), newline: true, line: line)

        case .PrintInline:
            try requireLineStart("??")
            _ = popCurrentToken()
            return PrintNode(items: try parsePrintItems(), newline: false, line: line)

        case .If:
            return try parseIf(line: line)
        case .While:
            return try parseWhile(line: line)
        case .For:
            return try parseFor(line: line)

        default:
            throw unexpected(peekCurrentToken().kind)
        }
    }

    private func parseAssignmentOrCall(line: Int) throws -> StmtNode {
        let start = index
        let name = try readIdentifier()

        if peekCurrentToken().kind == .ParensOpen {
            return try parseFunctionCall(callee: name)
        }

        var target: LValue = .variable(name)
        while peekCurrentToken().kind == .BracketOpen {
            _ = popCurrentToken()
            let subscriptExpr = try parseExpression()
            try consume(.BracketClose)
            target = .element(target, index: subscriptExpr)
        }

        // A statement starting 'A.row' can only be an attempt to assign to a dimension.
        // Say so, rather than letting it fall through to a bare "Unexpected '.'".
        if peekCurrentToken().kind == .Dot {
            _ = popCurrentToken()
            throw ParseError.AttributeNotAssignable((try? readIdentifier()) ?? "")
        }

        switch peekCurrentToken().kind {
        case .Assign:
            _ = popCurrentToken()
            // parseInitializer, not parseExpression: an assignment takes a brace literal
            // exactly as a declaration does. It falls through to parseExpression when the
            // right-hand side does not open with '{', so every other assignment is
            // unaffected.
            return AssignNode(target: target, op: .colon, expr: try parseInitializer(), line: line)
        case .PlusAssign:
            _ = popCurrentToken()
            return CompoundAssignNode(target: target, op: .add, expr: try parseExpression(), line: line)
        case .MinusAssign:
            _ = popCurrentToken()
            return CompoundAssignNode(target: target, op: .subtract, expr: try parseExpression(), line: line)
        case .Operator("="):
            _ = popCurrentToken()
            return AssignNode(target: target, op: .equals, expr: try parseInitializer(), line: line)
        default:
            index = start
            throw unexpected(peekCurrentToken().kind)
        }
    }

    private func parseReturn(line: Int) throws -> StmtNode {
        try consume(.Return)
        guard let prototype = enclosingPrototype else {
            throw ParseError.ReturnOutsideFunction
        }

        guard tokensAvailable,
              startsTerm(at: index),
              !looksLikeNewStatement(at: index) else {
            return ReturnNode(prototype: prototype, exprs: [], line: line)
        }

        if peekCurrentToken().kind == .ParensOpen, parenGroupHasTopLevelComma(at: index) {
            return ReturnNode(prototype: prototype,
                              exprs: try parseParensList(read: parseExpression),
                              line: line)
        }
        return ReturnNode(prototype: prototype, exprs: [try parseExpression()], line: line)
    }

    private func parenGroupHasTopLevelComma(at start: Int) -> Bool {
        var depth = 0
        var i = start
        while i < tokens.count {
            switch tokens[i].kind {
            case .ParensOpen, .BracketOpen:
                depth += 1
            case .ParensClose, .BracketClose:
                depth -= 1
                if depth == 0 { return false }
            case .Comma where depth == 1:
                return true
            default:
                break
            }
            i += 1
        }
        return false
    }

    private func parseIf(line: Int) throws -> StmtNode {
        try consume(.If)
        var branches = [IfNode.Branch(condition: try parseExpression(), body: try parseSubScope())]

        while match(.ElseIf) {
            branches.append(IfNode.Branch(condition: try parseExpression(), body: try parseSubScope()))
        }

        var elseBody: [StmtNode]? = nil
        if match(.Else) {
            elseBody = try parseSubScope()
        }
        return IfNode(branches: branches, elseBody: elseBody, line: line)
    }

    private func parseWhile(line: Int) throws -> StmtNode {
        try consume(.While)
        let condition = try parseExpression()
        return WhileNode(condition: condition, body: try parseLoopBody(), line: line)
    }

    private func parseFor(line: Int) throws -> StmtNode {
        try consume(.For)
        let variable = try readIdentifier()

        // 'for i < n' is shorthand for the loop that gets written most often,
        // 'for i : 0 to n - 1'. It is pure sugar: it expands to exactly that node, so
        // nothing downstream knows the difference - which is what lets 'step' ride along
        // unchanged, meaning here exactly what it means in the full form.
        if match(.Operator("<")) {
            let bound = try parseExpression()
            var step: ExprNode? = nil
            if match(.Step) {
                step = try parseExpression()
            }
            return ForNode(variable: variable,
                           start: IntNode(value: 0),
                           end: BinaryOpNode(op: .subtract, lhs: bound, rhs: IntNode(value: 1)),
                           step: step,
                           body: try parseLoopBody(),
                           line: line)
        }

        try consume(.Assign)
        let start = try parseExpression()
        try consume(.To)
        let end = try parseExpression()

        var step: ExprNode? = nil
        if match(.Step) {
            step = try parseExpression()
        }

        return ForNode(variable: variable, start: start, end: end, step: step,
                       body: try parseLoopBody(), line: line)
    }

    func parsePrintItems() throws -> [ExprNode] {
        var items = [ExprNode]()
        while tokensAvailable,
              startsTerm(at: index),
              !startsLine(at: index),
              !looksLikeNewStatement(at: index) {
            items.append(try isPrecisionDirective(at: index) ? parsePrecision()
                                                             : parseExpression())
        }
        return items
    }

    // 'prec' is recognized only here, as the head of a print item and only when a '('
    // follows, so it stays an ordinary name everywhere else - a variable called 'prec'
    // still works, and '? prec' still prints it. The one collision it cannot resolve is a
    // user function of the same name, which the checker refuses outright.
    private func isPrecisionDirective(at position: Int) -> Bool {
        guard position < tokens.count,
              case let .Identifier(name) = tokens[position].kind,
              name.lowercased() == "prec" else { return false }
        return position + 1 < tokens.count && tokens[position + 1].kind == .ParensOpen
    }

    private func parsePrecision() throws -> ExprNode {
        _ = popCurrentToken()
        try consume(.ParensOpen)
        let places = try parseExpression()
        try consume(.ParensClose)
        return PrecisionNode(places: places)
    }

    func parseExpression() throws -> ExprNode {
        return try parseBinaryOp(node: try parsePrimary())
    }

    func parsePrimary() throws -> ExprNode {
        switch peekCurrentToken().kind {
        case .Identifier:
            return try parseIdentifierExpression()

        // The type keywords are already keywords by the time the parser sees them, so
        // 'int(x)' could never reach parseFunctionCall the way an identifier does - the
        // conversions were implemented on both sides and simply unreachable. In an
        // expression a type keyword can only be a conversion: a declaration is decided a
        // level up, in parseStatement, before parsePrimary is ever called.
        case .Int, .Real, .Char:
            // Not a `where` clause on the case: in a comma-separated pattern list Swift
            // binds `where` to the last pattern only, so `.Int` would slip through it.
            guard peekNextToken().kind == .ParensOpen else {
                throw unexpected(peekCurrentToken().kind)
            }
            let name = describe(popCurrentToken().kind)
            return try parseFunctionCall(callee: name)
        case .IntLiteral(let value):
            _ = popCurrentToken(); return IntNode(value: Int(value))
        case .RealLiteral(let value):
            _ = popCurrentToken(); return RealNode(value: value)
        case .StringLiteral(let value):
            _ = popCurrentToken(); return StringNode(value: value)
        case .ParensOpen:
            _ = popCurrentToken()
            let expression = try parseExpression()
            try consume(.ParensClose)
            return expression
        case .Operator("-"):
            _ = popCurrentToken()
            let operand = try parsePrimary()
            if let int = operand as? IntNode { return IntNode(value: -int.value) }
            if let real = operand as? RealNode { return RealNode(value: -real.value) }
            return BinaryOpNode(op: .subtract, lhs: IntNode(value: 0), rhs: operand)
        default:
            throw unexpected(peekCurrentToken().kind)
        }
    }

    private func parseIdentifierExpression() throws -> ExprNode {
        let name = try readIdentifier()

        var node: ExprNode = peekCurrentToken().kind == .ParensOpen
            ? try parseFunctionCall(callee: name)
            : VariableNode(name: name)

        // '[' and '.' are both postfix, so they share one loop and chain in any order:
        // 'M[k].row' asks for the dimensions of a row, 'A.dim(i)' for those of A.
        postfix: while true {
            switch peekCurrentToken().kind {
            case .BracketOpen:
                _ = popCurrentToken()
                let subscriptExpr = try parseExpression()
                try consume(.BracketClose)
                node = IndexNode(base: node, index: subscriptExpr)
            case .Dot:
                _ = popCurrentToken()
                node = try parseAttribute(of: node)
            default:
                break postfix
            }
        }
        return node
    }

    // 'row' and 'col' are recognized only here, immediately after a dot, so they stay
    // ordinary identifiers everywhere else - a program with a variable named 'col' keeps
    // working. 'dim' takes its axis in parentheses because that axis is an expression.
    private func parseAttribute(of base: ExprNode) throws -> ExprNode {
        let name = try readIdentifier()
        switch name.lowercased() {
        case "row":
            return DimNode(base: base, axis: IntNode(value: 0), spelling: name)
        case "col":
            return DimNode(base: base, axis: IntNode(value: 1), spelling: name)
        case "dim":
            guard peekCurrentToken().kind == .ParensOpen else {
                throw ParseError.DimNeedsAnAxis
            }
            try consume(.ParensOpen)
            let axis = try parseExpression()
            try consume(.ParensClose)
            return DimNode(base: base, axis: axis, spelling: name)
        default:
            throw ParseError.UnknownAttribute(name)
        }
    }

    func parseFunctionCall(callee: String) throws -> CallNode {
        let line = peekCurrentToken().line
        return CallNode(callee: callee, arguments: try parseParensList(read: parseExpression), line: line)
    }

    let operatorPrecedence: [String: Int] = [
        "|": 10,
        "&": 20,
        "=": 30, "!=": 30, "<": 30, ">": 30, "<=": 30, ">=": 30,
        "+": 40, "-": 40,
        "*": 50, "/": 50, "%": 50,
        "^": 60
    ]
    let rightAssociative: Set<String> = ["^"]

    func getCurrentTokenPrecedence() -> Int {
        guard case let TokenKind.Operator(op) = peekCurrentToken().kind else { return -1 }
        if op == "<" && looksLikeMultiAssignHeader(at: index) { return -1 }
        return operatorPrecedence[op] ?? -1
    }

    func parseBinaryOp(node: ExprNode, exprPrecedence: Int = 0) throws -> ExprNode {
        var lhs = node
        while true {
            let tokenPrecedence = getCurrentTokenPrecedence()
            if tokenPrecedence < exprPrecedence { return lhs }

            guard case let TokenKind.Operator(spelling) = popCurrentToken().kind,
                  let op = BinaryOpNode.Op(rawValue: spelling) else {
                throw unexpected(peekCurrentToken().kind)
            }

            var rhs = try parsePrimary()
            let nextPrecedence = getCurrentTokenPrecedence()
            if tokenPrecedence < nextPrecedence {
                rhs = try parseBinaryOp(node: rhs, exprPrecedence: tokenPrecedence + 1)
            } else if tokenPrecedence == nextPrecedence && rightAssociative.contains(spelling) {
                rhs = try parseBinaryOp(node: rhs, exprPrecedence: tokenPrecedence)
            }

            lhs = BinaryOpNode(op: op, lhs: lhs, rhs: rhs)
        }
    }

    func parseParensList<T>(read: () throws -> T) throws -> [T] {
        try consume(.ParensOpen)
        if match(.ParensClose) { return [] }

        var list = [T]()
        while true {
            list.append(try read())
            if match(.Comma) { continue }
            break
        }
        try consume(.ParensClose)
        return list
    }

    func parseAngleList<T>(read: () throws -> T) throws -> [T] {
        try consume(.Operator("<"))
        if match(.Operator(">")) { return [] }

        var list = [T]()
        while true {
            list.append(try read())
            if match(.Comma) { continue }
            break
        }
        try consume(.Operator(">"))
        return list
    }

    private func looksLikeMultiAssignHeader(at start: Int) -> Bool {
        var i = start + 1
        guard i < tokens.count, case .Identifier = tokens[i].kind else { return false }
        i += 1
        while i < tokens.count, case .Comma = tokens[i].kind {
            i += 1
            guard i < tokens.count, case .Identifier = tokens[i].kind else { return false }
            i += 1
        }
        guard i < tokens.count, tokens[i].kind == .Operator(">") else { return false }
        i += 1
        guard i < tokens.count, tokens[i].kind == .Assign else { return false }
        return true
    }

    private func looksLikeNewStatement(at start: Int) -> Bool {
        guard start < tokens.count else { return false }

        if case .Identifier = tokens[start].kind, start + 1 < tokens.count {
            switch tokens[start + 1].kind {
            case .Assign, .PlusAssign, .MinusAssign, .Operator("="): return true
            default: break
            }
        }
        if tokens[start].kind == .Operator("<"), looksLikeMultiAssignHeader(at: start) {
            return true
        }
        return false
    }

    // Takes a position rather than a kind because one case needs a token of lookahead:
    // 'int' opens a term when it is 'int(x)', the conversion call, but not when it is
    // 'int k : 5', a declaration that belongs to the statement parser.
    private func startsTerm(at position: Int) -> Bool {
        guard position < tokens.count else { return false }
        switch tokens[position].kind {
        case .IntLiteral, .RealLiteral, .StringLiteral, .Identifier, .ParensOpen: return true
        case .Operator(let op): return op == "-"
        case .Int, .Real, .Char:
            return position + 1 < tokens.count && tokens[position + 1].kind == .ParensOpen
        default: return false
        }
    }

    private func requireLineStart(_ command: String) throws {
        guard startsLine(at: index) else {
            throw ParseError.PrintNotAtLineStart(command)
        }
    }

    private func startsLine(at index: Int) -> Bool {
        guard index < tokens.count else { return false }
        guard index > 0 else { return true }
        return tokens[index - 1].line < tokens[index].line
    }
}
