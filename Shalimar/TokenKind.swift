import Foundation

enum TokenKind: Equatable {
    case IntLiteral(Int32)
    case RealLiteral(Double)
    case StringLiteral(String)
    case Identifier(String)

    case Operator(String)

    case Assign
    case PlusAssign, MinusAssign
    case PrintLine, PrintInline
    case ParensOpen, ParensClose
    case BraceOpen, BraceClose
    case BracketOpen, BracketClose
    case Comma, Dot

    case If, Else, While, For, To, Step, Fun, Return, Uses
    case Break, Continue
    case Int, Real, Char

    case EndOfInput
}

struct Token {
    let kind: TokenKind
    let line: Int
}

enum LexIssue: Error {
    case malformedNumber(String)
    case integerOutOfRange(String)
    case bangIsNotACommand
    case unclosedString
}

typealias TokenGenerator = (String) throws -> TokenKind?

let tokenList: [(String, TokenGenerator)] = [
    ("//[^\n]*", { _ in nil }),
    ("[ \t\r\n]+", { _ in nil }),

    // A literal is closed by a quote on its own line. Both halves of that matter:
    //
    // The closing quote used to be optional, so a stray one swallowed everything after
    // it and surfaced as a parse error far below, pointing at a line that was fine.
    //
    // Excluding the newline is what puts the error on the line the quote is actually
    // on. With [^"]* the opening quote simply reached forward to the next quote in the
    // file - one line down, or twenty - and the text between them, program and all,
    // became the string. A literal that spans lines has no use here anyway: there are
    // no escapes, and layout already carries meaning for the print rule.
    ("\"[^\"\n]*\"", { raw in
        return .StringLiteral(String(raw.dropFirst().dropLast()))
    }),
    ("\"", { _ in throw LexIssue.unclosedString }),

    ("[a-zA-Z_][a-zA-Z0-9_]*", { raw in
        switch raw.lowercased() {
        case "if":     return .If
        case "else":   return .Else
        case "while":  return .While
        case "for":    return .For
        case "to":     return .To
        case "step":   return .Step
        case "fun":    return .Fun
        case "uses":   return .Uses
        case "return": return .Return
        case "break":    return .Break
        case "continue": return .Continue
        case "int":    return .Int
        case "real":   return .Real
        case "char":   return .Char
        default:       return .Identifier(raw)
        }
    }),

    // The exponent is scanned loosely - '[0-9]*' rather than '[0-9]+' - so that '1e' and
    // '1e-' are captured whole and reported as malformed numbers, the same way '1.2.3' is.
    // Scanning it strictly would leave '1e' to split into the number 1 and an identifier
    // 'e', desyncing everything after it. Double is what actually arbitrates.
    ("[0-9][0-9.]*([eE][+-]?[0-9]*)?", { raw in
        // An exponent always means a real: '1e10' is far past an int's range, and exponent
        // notation is only ever reached for a magnitude an int could not carry anyway.
        if raw.contains(".") || raw.lowercased().contains("e") {
            guard let value = Double(raw), value.isFinite else {
                throw LexIssue.malformedNumber(raw)
            }
            return .RealLiteral(value)
        }
        guard let wide = Int64(raw) else { throw LexIssue.integerOutOfRange(raw) }
        guard let value = Int32(exactly: wide) else { throw LexIssue.integerOutOfRange(raw) }
        return .IntLiteral(value)
    }),

    ("\\+:", { _ in .PlusAssign }),
    ("-:", { _ in .MinusAssign }),
    // The three two-character comparisons must all stay above the single-character
    // operator class at the foot of this list. Patterns are tried in order and the first
    // match is consumed, so were '[-+*/%^=<>&|]' reached first it would take the '<' of
    // '<=' and leave the '=' behind to be read as a second, equality comparison.
    ("!=", { _ in .Operator("!=") }),
    ("<=", { _ in .Operator("<=") }),
    (">=", { _ in .Operator(">=") }),
    ("\\?\\?", { _ in .PrintInline }),
    ("\\?", { _ in .PrintLine }),
    ("!", { _ in throw LexIssue.bangIsNotACommand }),

    ("\\(", { _ in .ParensOpen }),
    ("\\)", { _ in .ParensClose }),
    ("\\{", { _ in .BraceOpen }),
    ("\\}", { _ in .BraceClose }),
    ("\\[", { _ in .BracketOpen }),
    ("\\]", { _ in .BracketClose }),
    (",", { _ in .Comma }),
    // Must stay below the number rule: '[0-9][0-9.]*' is tried first, so the dot in
    // '1.5' is eaten as part of the numeral and only a dot that cannot start a number
    // reaches here. That is exactly the dot of 'A.row'.
    ("\\.", { _ in .Dot }),
    (":", { _ in .Assign }),
    ("[-+*/%^=<>&|]", { .Operator($0) }),
]

var expressions = [String: NSRegularExpression]()
extension String {
    func match(regex: String) -> String? {
        let expression: NSRegularExpression
        if let exists = expressions[regex] {
            expression = exists
        } else {
            expression = try! NSRegularExpression(pattern: "^\(regex)", options: [])
            expressions[regex] = expression
        }

        let range = expression.rangeOfFirstMatch(in: self, options: [], range: NSMakeRange(0, self.utf16.count))
        guard range.location != NSNotFound, let r = Range(range, in: self) else { return nil }
        return String(self[r])
    }
}

class Lexer {
    var content: String
    private var line = 1
    private var tokenLine = 1
    private(set) var lexError: String?

    init(input: String) {
        self.content = input
    }

    private static func codePoint(of char: Character) -> String {
        guard let scalar = char.unicodeScalars.first else { return "U+?" }
        var hex = String(scalar.value, radix: 16, uppercase: true)
        while hex.count < 4 { hex = "0" + hex }
        return "U+" + hex
    }

    private func message(_ text: String) -> String {
        return "Error: line \(tokenLine): \(text)"
    }

    func tokenize() -> [Token] {
        var tokens = [Token]()

        while (content.count > 0) {
            var matched = false
            tokenLine = line

            for (pattern, generator) in tokenList {
                guard let m = content.match(regex: pattern) else { continue }

                do {
                    if let kind = try generator(m) {
                        tokens.append(Token(kind: kind, line: tokenLine))
                    }
                } catch LexIssue.malformedNumber(let run) {
                    lexError = message("Malformed number '\(run)'")
                    return tokens
                } catch LexIssue.integerOutOfRange(let run) {
                    lexError = message("'\(run)' is too big for int - add '.'")
                    return tokens
                } catch LexIssue.unclosedString {
                    lexError = message("Unclosed string - add '\"'")
                    return tokens
                } catch {
                    lexError = message("'!' is not a command - use '??' or '!='")
                    return tokens
                }

                line += m.reduce(0) { $1 == "\n" ? $0 + 1 : $0 }

                let range = content.startIndex..<content.index(content.startIndex, offsetBy: m.count)
                content.removeSubrange(range)
                matched = true
                break
            }

            if !matched {
                let char = content.first ?? " "
                lexError = message("Unexpected character '\(char)' (\(Lexer.codePoint(of: char)))")
                return tokens
            }
        }

        return tokens
    }
}
