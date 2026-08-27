import Foundation

struct RuntimeError: Error, CustomStringConvertible {
    let message: String
    let line: Int
    var description: String {
        line > 0 ? "Error: line \(line): \(message)" : "Error: \(message)"
    }
}

final class ArrayRef {
    var elements: [Value]
    init(_ elements: [Value]) { self.elements = elements }
}

final class Box {
    var value: Value
    init(_ value: Value) { self.value = value }
}

enum Value {
    case int(Int32)
    case real(Double)
    case char(UInt8)
    case array(ArrayRef)

    static func zero(of type: ShalimarType, sizes: [Int]) -> Value {
        switch type {
        case .int:  return .int(0)
        case .real: return .real(0)
        case .char: return .char(0)
        case .array(let element):
            let count = sizes.first ?? 0
            let rest = Array(sizes.dropFirst())
            return .array(ArrayRef((0..<count).map { _ in Value.zero(of: element, sizes: rest) }))
        }
    }

    var isTruthy: Bool {
        switch self {
        case .int(let v):  return v != 0
        case .real(let v): return v != 0
        case .char(let v): return v != 0
        case .array:       return true
        }
    }

    var text: String {
        switch self {
        case .int(let v):  return "\(v)"
        case .real(let v): return Value.fixed(v, places: Value.scalarPlaces)
        case .char(let v): return v == 0 ? "" : String(UnicodeScalar(v))
        case .array(let ref):
            if case .char = ref.elements.first {
                var bytes: [UInt8] = []
                for element in ref.elements {
                    guard case .char(let byte) = element, byte != 0 else { break }
                    bytes.append(byte)
                }
                return String(decoding: bytes, as: UTF8.self)
            }
            return "[" + ref.elements.map { $0.text }.joined(separator: ", ") + "]"
        }
    }
}

extension Value {
    var isCharArray: Bool {
        guard case .array(let ref) = self, case .char = ref.elements.first else { return false }
        return true
    }

    // A real always prints to a fixed number of decimal places rather than through
    // Swift's shortest-round-trip description, which gives "1.0" one width and
    // "0.3333333333333333" another - unreadable stacked in a column, and inconsistent
    // even on its own line. A grid cell gets one place fewer than a scalar: a matrix row
    // has to fit the screen, and every place spends a character in every column.
    static let defaultScalarPlaces = 7
    static let defaultGridPlaces = 6

    // '? prec(n)' overrides both, and a negative argument restores the pair above. These
    // are static because `text` and `cell` are context-free computed properties reached
    // from everywhere; Interpreter.run() resets them on entry so one program's setting
    // cannot leak into the next run in the same process.
    private(set) static var scalarPlaces = defaultScalarPlaces
    private(set) static var gridPlaces = defaultGridPlaces

    // prec(n) runs -1 through 24 and clamps to that range at both ends, so -1 is the
    // reset and no argument is ever refused. 24 is past the ~17 significant digits a
    // Double actually carries - beyond it the places are fabricated zeros - while still
    // leaving room to read a tolerance like 1e-20, which is why precision gets raised.
    static let minPlaces = -1
    static let maxPlaces = 24

    static func setPlaces(_ requested: Int) {
        let places = Swift.max(minPlaces, Swift.min(requested, maxPlaces))
        guard places >= 0 else {
            scalarPlaces = defaultScalarPlaces
            gridPlaces = defaultGridPlaces
            return
        }
        scalarPlaces = places
        gridPlaces = places
    }

    // Past 1e15 a Double has no significant digits left to land after the point, so
    // fixed notation would print hundreds of fabricated ones (1e300 renders as 309
    // characters). Those, and the non-finite values, keep the compact spelling.
    fileprivate static func fixed(_ v: Double, places: Int) -> String {
        guard v.isFinite, abs(v) < 1e15 else { return "\(v)" }
        return String(format: "%.\(places)f", v)
    }

    fileprivate var cell: String {
        if case .real(let v) = self { return Value.fixed(v, places: Value.gridPlaces) }
        return text
    }

    fileprivate static func rows(of ref: ArrayRef) -> [[String]] {
        guard case .array = ref.elements.first else {
            return [ref.elements.map { $0.cell }]
        }
        return ref.elements.flatMap { element -> [[String]] in
            guard case .array(let inner) = element else { return [[element.cell]] }
            return rows(of: inner)
        }
    }

    var grid: (text: String, isMultiRow: Bool)? {
        guard case .array(let ref) = self, !isCharArray else { return nil }
        let rows = Value.rows(of: ref)
        let width = rows.flatMap { $0 }.map { $0.count }.max() ?? 0
        let text = rows.map { row in
            row.map { String(repeating: " ", count: width - $0.count) + $0 }
               .joined(separator: "  ")
        }.joined(separator: "\n")
        return (text, rows.count > 1)
    }
}

final class Interpreter {
    var output: (String) -> Void = { print($0, terminator: "") }

    // A separate channel for the interpreter's own failures. They used to travel the
    // output callback alongside the program's prints, which left the console unable to
    // tell a result from a crash without inspecting the text.
    var diagnostic: ((String) -> Void)?

    private var prototypes: [String: PrototypeNode] = [:]
    private var bodies: [String: [StmtNode]] = [:]
    private var globals: [String: Box] = [:]
    private var scopes: [[String: Box]] = []
    private var currentLine = 0

    private var callDepth: [String: Int] = [:]
    private var totalCallDepth = 0
    private static let totalDepthLimit = 1024

    private static let constants: [String: Value] = ["pi": .real(Double.pi), "e": .real(M_E)]

    // How a statement finished. Anything but .normal stops the enclosing statement
    // list and travels outward until something claims it: a loop claims .broke and
    // .continued, a call claims .returned. The parser refuses 'break'/'continue'
    // outside a loop, so neither can reach a call boundary unclaimed.
    private enum Flow {
        case normal
        case returned([Value])
        case broke
        case continued
    }

    func run(_ program: [Node]) {
        // Precision lives in static storage, so a '? prec(n)' left set by the previous
        // program in this process must not carry into this one.
        Value.setPlaces(-1)
        do {
            for case let function as FunctionNode in program {
                prototypes[function.prototype.name] = function.prototype
                bodies[function.prototype.name] = function.body
            }
            for case let declaration as DeclareNode in program {
                globals[declaration.name] = Box(try initialValue(for: declaration))
            }
            guard let main = prototypes["main"] else {
                throw RuntimeError(message: "No main() function defined", line: 0)
            }
            _ = try call(main, arguments: [], line: main.line)
        } catch let error as RuntimeError {
            (diagnostic ?? output)("\(error)\n")
        } catch {
            (diagnostic ?? output)("Error: \(error)\n")
        }
    }

    private func lookup(_ name: String, _ line: Int) throws -> Box {
        for scope in scopes.reversed() {
            if let box = scope[name] { return box }
        }
        if let box = globals[name] { return box }
        // Checked as read-only, so a fresh Box each time is safe: nothing can write it.
        if let constant = Interpreter.constants[name] { return Box(constant) }
        throw RuntimeError(message: "Undefined variable '\(name)'", line: line)
    }

    private func define(_ name: String, _ box: Box) {
        guard !scopes.isEmpty else { globals[name] = box; return }
        scopes[scopes.count - 1][name] = box
    }

    private func existsInScope(_ name: String) -> Bool {
        scopes.contains { $0[name] != nil } || globals[name] != nil
    }

    private func call(_ prototype: PrototypeNode, arguments: [Value],
                      boxes: [Int: Box] = [:], line: Int) throws -> [Value] {
        let name = prototype.name

        let limit = max(1, 256 / (prototype.inputs.count + 1))
        let depth = (callDepth[name] ?? 0) + 1
        guard depth <= limit else {
            throw RuntimeError(message: "Recursion too deep in '\(name)' (limit \(limit))", line: line)
        }
        guard totalCallDepth < Interpreter.totalDepthLimit else {
            throw RuntimeError(message: "Call stack too deep (limit \(Interpreter.totalDepthLimit))", line: line)
        }
        callDepth[name] = depth
        totalCallDepth += 1

        let savedScopes = scopes
        scopes = [[:]]
        defer {
            scopes = savedScopes
            callDepth[name] = depth - 1
            totalCallDepth -= 1
        }

        for (i, parameter) in prototype.inputs.enumerated() where i < arguments.count {
            scopes[0][parameter.name] = boxes[i] ?? Box(arguments[i])
        }

        guard let body = bodies[name] else {
            throw RuntimeError(message: "Unknown function '\(name)'", line: line)
        }
        if case .returned(let values) = try execute(body) {
            return values
        }
        return []
    }

    private func execute(_ statements: [StmtNode]) throws -> Flow {
        for statement in statements {
            let flow = try execute(statement)
            if case .normal = flow { continue }
            return flow
        }
        return .normal
    }

    private func execute(_ statement: StmtNode) throws -> Flow {
        currentLine = statement.line

        switch statement {
        case let node as DeclareNode:
            define(node.name, Box(try initialValue(for: node)))

        case let node as AssignNode:
            try store(try evaluate(node.expr), into: node.target, line: node.line)

        case let node as CompoundAssignNode:
            let current = try load(node.target, line: node.line)
            let delta = try evaluate(node.expr)
            let op: BinaryOpNode.Op = node.op == .add ? .add : .subtract
            try store(try apply(op, current, delta, node.line), into: node.target, line: node.line)

        case let node as MultiAssignNode:
            let values = try callNode(node.call)
            for (i, target) in node.targets.enumerated() where i < values.count {
                try store(values[i], into: target, line: node.line)
            }

        case let node as ReturnNode:
            return .returned(try node.exprs.map { try evaluate($0) })

        case is BreakNode:
            return .broke

        case is ContinueNode:
            return .continued

        case let node as PrintNode:
            var text = ""
            for item in node.items {
                // A directive, not a value: it changes the precision and prints nothing,
                // and because items are handled in order it applies to the rest of this
                // same line - '? prec(12) x' shows x at twelve places.
                if let directive = item as? PrecisionNode {
                    guard case .int(let places) = try evaluate(directive.places) else {
                        throw RuntimeError(message: "prec() needs an int", line: node.line)
                    }
                    Value.setPlaces(Int(places))
                    continue
                }
                let value = try evaluate(item)
                guard let grid = value.grid else {
                    text += value.text + " "
                    continue
                }
                if grid.isMultiRow && !text.isEmpty { text += "\n" }
                text += grid.text + " "
            }
            output(text + (node.newline ? "\n" : ""))

        case let node as CallNode:
            _ = try callNode(node)

        case let node as IfNode:
            for branch in node.branches where try evaluate(branch.condition).isTruthy {
                return try inScope { try execute(branch.body) }
            }
            if let elseBody = node.elseBody {
                return try inScope { try execute(elseBody) }
            }

        case let node as WhileNode:
            while try evaluate(node.condition).isTruthy {
                switch try inScope({ try execute(node.body) }) {
                case .returned(let values): return .returned(values)
                case .broke:                return .normal
                case .continued, .normal:   break
                }
            }

        case let node as ForNode:
            return try executeFor(node)

        default:
            break
        }
        return .normal
    }

    private func inScope(_ body: () throws -> Flow) rethrows -> Flow {
        scopes.append([:])
        defer { scopes.removeLast() }
        return try body()
    }

    private func executeFor(_ node: ForNode) throws -> Flow {
        let start = try evaluate(node.start)
        let end = try evaluate(node.end)
        let step = try node.step.map { try evaluate($0) } ?? defaultStep(like: start)

        scopes.append([:])
        defer { scopes.removeLast() }
        let counter = Box(start)
        scopes[scopes.count - 1][node.variable] = counter

        if case .real(let startValue) = start,
           case .real(let endValue) = end,
           case .real(let stepValue) = step {
            for (value, role) in [(startValue, "start"), (endValue, "end"), (stepValue, "step")] {
                guard value.isFinite else {
                    throw RuntimeError(message: "Loop \(role) out of range: \(value)", line: node.line)
                }
            }
            guard stepValue != 0 else {
                throw RuntimeError(message: "Step value cannot be zero", line: node.line)
            }
            var n = 0.0
            while true {
                let value = startValue + n * stepValue
                if stepValue > 0 ? value > endValue : value < endValue { break }
                counter.value = .real(value)
                switch try execute(node.body) {
                case .returned(let values): return .returned(values)
                case .broke:                return .normal
                // The step belongs to the loop, not the body, so 'continue' still
                // advances the counter - it skips the rest of this pass, not the pass.
                case .continued, .normal:   break
                }
                n += 1
            }
            return .normal
        }

        guard case .int(let startValue) = start,
              case .int(let endValue) = end,
              case .int(let stepValue) = step else {
            throw RuntimeError(message: "A loop counter must be int or real", line: node.line)
        }
        guard stepValue != 0 else {
            throw RuntimeError(message: "Step value cannot be zero", line: node.line)
        }
        var i = startValue
        while stepValue > 0 ? i <= endValue : i >= endValue {
            counter.value = .int(i)
            switch try execute(node.body) {
            case .returned(let values): return .returned(values)
            case .broke:                return .normal
            case .continued, .normal:   break
            }
            let (next, overflow) = i.addingReportingOverflow(stepValue)
            if overflow { break }
            i = next
        }
        return .normal
    }

    private func defaultStep(like value: Value) -> Value {
        if case .real = value { return .real(1) }
        return .int(1)
    }

    private func initialValue(for node: DeclareNode) throws -> Value {
        var sizes: [Int] = []
        for size in node.sizes {
            let value = try evaluate(size)
            guard case .int(let count) = value else {
                throw RuntimeError(message: "'\(node.name)': size must be int", line: node.line)
            }
            guard count >= 1 else {
                throw RuntimeError(message: "'\(node.name)': size must be 1 or more, got \(count)",
                                   line: node.line)
            }
            sizes.append(Int(count))
        }

        var value = Value.zero(of: node.type, sizes: sizes)
        if let initial = node.initial {
            value = try fit(try evaluate(initial), into: value, line: node.line)
        }
        return value
    }

    /// Resets every element to the zero of its own type, in place and at any rank, keeping
    /// the array's storage and extents. Each element keeps its type because the extents are
    /// fixed at declaration and so is the element type - clearing is not retyping.
    private func clear(_ ref: ArrayRef) {
        for i in ref.elements.indices {
            switch ref.elements[i] {
            case .array(let inner): clear(inner)
            case .int:  ref.elements[i] = .int(0)
            case .real: ref.elements[i] = .real(0)
            case .char: ref.elements[i] = .char(0)
            }
        }
    }

    private func fit(_ source: Value, into target: Value, line: Int) throws -> Value {
        guard case .array(let targetRef) = target else { return source }
        guard case .array(let sourceRef) = source else { return target }

        let isChars = targetRef.elements.first.map { if case .char = $0 { return true } else { return false } } ?? false
        let capacity = isChars ? max(0, targetRef.elements.count - 1) : targetRef.elements.count

        for i in 0..<min(capacity, sourceRef.elements.count) {
            targetRef.elements[i] = try fit(sourceRef.elements[i], into: targetRef.elements[i], line: line)
        }
        return target
    }

    private func load(_ target: LValue, line: Int) throws -> Value {
        switch target {
        case .variable(let name):
            return try lookup(name, line).value
        case .element(let base, let index):
            let (ref, i) = try resolveElement(base, index, line: line)
            return ref.elements[i]
        }
    }

    private func store(_ value: Value, into target: LValue, line: Int) throws {
        switch target {
        case .variable(let name):
            guard existsInScope(name) else {
                define(name, Box(value))
                return
            }
            let box = try lookup(name, line)
            // Copy into the storage the variable already has rather than rebinding it.
            // Extents are fixed at declaration, and an array may be shared by reference
            // with a caller - swapping the storage out would resize it under them.
            if case .array(let existing) = box.value, case .array = value {
                // Clear first, so the literal is the array's whole new value rather than a
                // patch over the old one. Absence then means the same thing however it
                // arises - a gap inside the literal, or a literal that stops short of the
                // extents - instead of a gap writing 0 while a short literal left 5 behind.
                // Only the contents are reset; the storage itself is kept, which is what
                // still lets a by-reference parameter be filled without resizing it.
                clear(existing)
                _ = try fit(value, into: box.value, line: line)
                return
            }
            box.value = value
        case .element(let base, let index):
            let (ref, i) = try resolveElement(base, index, line: line)
            ref.elements[i] = value
        }
    }

    private func resolveElement(_ base: LValue, _ index: ExprNode, line: Int) throws -> (ArrayRef, Int) {
        let container = try load(base, line: line)
        guard case .array(let ref) = container else {
            throw RuntimeError(message: "Cannot index a scalar", line: line)
        }
        guard case .int(let raw) = try evaluate(index) else {
            throw RuntimeError(message: "An index must be int", line: line)
        }
        let i = Int(raw)
        guard i >= 0 && i < ref.elements.count else {
            throw RuntimeError(message: "Index \(i) out of range 0...\(ref.elements.count - 1)", line: line)
        }
        return (ref, i)
    }

    // Walks down 'axis' levels taking the first element at each, then counts. An axis the
    // value does not reach answers -1: that is what makes 'v.col' on a one-dimensional
    // array a legal question with a recognizable "there is no such dimension" answer,
    // rather than an error the caller has to avoid triggering. It is measured, not
    // declared, so a row substituted at run time is reported at its real length.
    private static func size(of value: Value, axis: Int) -> Int {
        guard axis >= 0, case .array(let ref) = value else { return -1 }
        if axis == 0 { return ref.elements.count }
        guard let first = ref.elements.first else { return -1 }
        return size(of: first, axis: axis - 1)
    }

    private func evaluate(_ expr: ExprNode) throws -> Value {
        switch expr {
        case let node as IntNode:
            guard let value = Int32(exactly: node.value) else {
                throw RuntimeError(message: "\(node.value) is too big for int - use real",
                                   line: currentLine)
            }
            return .int(value)

        case let node as RealNode:
            return .real(node.value)

        case let node as StringNode:
            var elements = node.value.utf8.map { Value.char($0) }
            elements.append(.char(0))
            return .array(ArrayRef(elements))

        case let node as VariableNode:
            return try lookup(node.name, currentLine).value

        case let node as IndexNode:
            let container = try evaluate(node.base)
            guard case .array(let ref) = container else {
                throw RuntimeError(message: "Cannot index a scalar", line: currentLine)
            }
            guard case .int(let raw) = try evaluate(node.index) else {
                throw RuntimeError(message: "An index must be int", line: currentLine)
            }
            let i = Int(raw)
            guard i >= 0 && i < ref.elements.count else {
                throw RuntimeError(message: "Index \(i) out of range 0...\(ref.elements.count - 1)",
                                   line: currentLine)
            }
            return ref.elements[i]

        case let node as DimNode:
            let container = try evaluate(node.base)
            guard case .int(let axis) = try evaluate(node.axis) else {
                throw RuntimeError(message: "'.\(node.spelling)' needs an int axis", line: currentLine)
            }
            return .int(Int32(clamping: Interpreter.size(of: container, axis: Int(axis))))

        case let node as ConvertNode:
            return try convert(try evaluate(node.expr), to: node.to)

        case let node as BinaryOpNode:
            return try apply(node.op, try evaluate(node.lhs), try evaluate(node.rhs), currentLine)

        case let node as ArrayLiteralNode:
            return .array(ArrayRef(try node.elements.map { try evaluate($0) }))

        case let node as CallNode:
            let values = try callNode(node)
            return values.first ?? .int(0)

        default:
            throw RuntimeError(message: "This expression cannot be evaluated", line: currentLine)
        }
    }

    private func convert(_ value: Value, to target: ShalimarType) throws -> Value {
        switch (value, target) {
        case (.int(let v), .real):
            return .real(Double(v))
        case (.real(let v), .int):
            guard v.isFinite, let truncated = Int32(exactly: v.rounded(.towardZero)) else {
                throw RuntimeError(message: "Cannot convert \(v) to int", line: currentLine)
            }
            return .int(truncated)

        // A char is its code point in the other direction. Widening out of a char always
        // succeeds - 0...255 fits both - so only the narrowing into one can fail, and it
        // is refused rather than wrapped: a wrapped code point is a wrong character that
        // looks like a right one.
        case (.char(let v), .int):
            return .int(Int32(v))
        case (.char(let v), .real):
            return .real(Double(v))
        case (.int(let v), .char):
            guard let byte = UInt8(exactly: v) else {
                throw RuntimeError(message: "\(v) is not a char (0 to 255)", line: currentLine)
            }
            return .char(byte)
        case (.real(let v), .char):
            guard v.isFinite, let byte = UInt8(exactly: v.rounded(.towardZero)) else {
                throw RuntimeError(message: "\(v) is not a char (0 to 255)", line: currentLine)
            }
            return .char(byte)

        default:
            return value
        }
    }

    // The text a string actually holds: everything before the terminator, which is what
    // makes a name in a char[20] equal to the same name in a char[128]. Returns nil for
    // anything that is not a char array, so the numeric paths below are unaffected.
    private static func content(of value: Value) -> [UInt8]? {
        guard case .array(let ref) = value, case .char = ref.elements.first else { return nil }
        var bytes: [UInt8] = []
        for element in ref.elements {
            guard case .char(let byte) = element, byte != 0 else { break }
            bytes.append(byte)
        }
        return bytes
    }

    private static func string(from bytes: [UInt8]) -> Value {
        return .array(ArrayRef(bytes.map { Value.char($0) } + [.char(0)]))
    }

    private func apply(_ op: BinaryOpNode.Op, _ lhs: Value, _ rhs: Value, _ line: Int) throws -> Value {
        func truth(_ condition: Bool) -> Value { .int(condition ? 1 : 0) }

        switch op {
        case .and: return truth(lhs.isTruthy && rhs.isTruthy)
        case .or:  return truth(lhs.isTruthy || rhs.isTruthy)
        default:   break
        }

        // Ordering is by byte, so it is ASCII order: every uppercase letter sorts before
        // every lowercase one. That is the conventional rule, and the reason a name list
        // wants folding to one case before it is sorted.
        if let a = Interpreter.content(of: lhs), let b = Interpreter.content(of: rhs) {
            switch op {
            case .add:      return Interpreter.string(from: a + b)
            case .equal:    return truth(a == b)
            case .notEqual: return truth(a != b)
            case .less:     return truth(a.lexicographicallyPrecedes(b))
            case .greater:  return truth(b.lexicographicallyPrecedes(a))
            case .lessEqual:    return truth(!b.lexicographicallyPrecedes(a))
            case .greaterEqual: return truth(!a.lexicographicallyPrecedes(b))
            default:
                throw RuntimeError(message: "'\(op.rawValue)' does not apply to strings", line: line)
            }
        }

        if case .int(let a) = lhs, case .int(let b) = rhs {
            switch op {
            case .add:      return .int(try guarded(a.addingReportingOverflow(b), op, line))
            case .subtract: return .int(try guarded(a.subtractingReportingOverflow(b), op, line))
            case .multiply: return .int(try guarded(a.multipliedReportingOverflow(by: b), op, line))
            case .divide:
                guard b != 0 else { throw RuntimeError(message: "Division by zero", line: line) }
                return .int(try guarded(a.dividedReportingOverflow(by: b), op, line))
            case .modulus:
                guard b != 0 else { throw RuntimeError(message: "Division by zero", line: line) }
                return .int(try guarded(a.remainderReportingOverflow(dividingBy: b), op, line))
            case .power:
                guard b >= 0 else { throw RuntimeError(message: "Negative power needs reals", line: line) }
                var result: Int32 = 1
                for _ in 0..<b { result = try guarded(result.multipliedReportingOverflow(by: a), op, line) }
                return .int(result)
            case .equal:    return truth(a == b)
            case .notEqual: return truth(a != b)
            case .less:     return truth(a < b)
            case .greater:  return truth(a > b)
            case .lessEqual:    return truth(a <= b)
            case .greaterEqual: return truth(a >= b)
            case .and, .or: break
            }
        }

        let a = try number(lhs, line)
        let b = try number(rhs, line)
        switch op {
        case .add:      return .real(a + b)
        case .subtract: return .real(a - b)
        case .multiply: return .real(a * b)
        case .divide:   return .real(a / b)
        case .modulus:  return .real(a.truncatingRemainder(dividingBy: b))
        case .power:    return .real(pow(a, b))
        case .equal:    return truth(a == b)
        case .notEqual: return truth(a != b)
        case .less:     return truth(a < b)
        case .greater:  return truth(a > b)
        case .lessEqual:    return truth(a <= b)
        case .greaterEqual: return truth(a >= b)
        case .and, .or: return truth(false)
        }
    }

    private func guarded(_ result: (partialValue: Int32, overflow: Bool),
                         _ op: BinaryOpNode.Op, _ line: Int) throws -> Int32 {
        guard !result.overflow else {
            throw RuntimeError(message: "int overflow in '\(op.rawValue)' - use real", line: line)
        }
        return result.partialValue
    }

    private func number(_ value: Value, _ line: Int) throws -> Double {
        switch value {
        case .real(let v): return v
        case .int(let v):  return Double(v)
        case .char(let v): return Double(v)
        case .array:       throw RuntimeError(message: "Expected a number, got an array", line: line)
        }
    }

    private func callNode(_ node: CallNode) throws -> [Value] {
        let line = node.line
        currentLine = line

        // The program's own function wins, as the checker decided. Asking the
        // builtins first would run sqrt for a program that defined its own.
        if prototypes[node.callee] == nil, let values = try callBuiltin(node) {
            return values
        }

        guard let prototype = prototypes[node.callee] else {
            throw RuntimeError(message: "Unknown function '\(node.callee)'", line: line)
        }

        var arguments: [Value] = []
        var boxes: [Int: Box] = [:]
        var writeBacks: [(Box, LValue)] = []
        for (i, argument) in node.arguments.enumerated() {
            let value = try evaluate(argument)
            arguments.append(value)
            if i < prototype.inputs.count,
               prototype.inputs[i].byReference,
               !prototype.inputs[i].type.isReference,
               let target = lvalue(of: argument) {
                let box = Box(value)
                boxes[i] = box
                writeBacks.append((box, target))
            }
        }

        let outputs = try call(prototype, arguments: arguments, boxes: boxes, line: line)
        for (box, target) in writeBacks {
            try store(box.value, into: target, line: line)
        }
        return outputs
    }

    private func lvalue(of expr: ExprNode) -> LValue? {
        switch expr {
        case let node as VariableNode:
            return .variable(node.name)
        case let node as IndexNode:
            guard let base = lvalue(of: node.base) else { return nil }
            return .element(base, index: node.index)
        default:
            return nil
        }
    }

    // **No `uses` check here, and that is deliberate.** A library function is callable
    // only in a file that borrowed it, but the borrow list is the checker's and the
    // gate is at Check.checkCall: an unborrowed name never reaches a running program,
    // because both the app and the harness refuse to run once the checker reports an
    // error. Testing it a second time here would be a second place to keep in step
    // with the table, and the only way to reach it would be a bug in the first.
    private func callBuiltin(_ node: CallNode) throws -> [Value]? {
        let line = node.line

        switch node.callee {
        case "len":
            guard case .array(let ref) = try evaluate(node.arguments[0]) else {
                throw RuntimeError(message: "len() needs an array", line: line)
            }
            return [.int(Int32(ref.elements.count))]

        case "int":
            return [try convert(try evaluate(node.arguments[0]), to: .int)]
        case "real":
            return [try convert(try evaluate(node.arguments[0]), to: .real)]
        case "char":
            return [try convert(try evaluate(node.arguments[0]), to: .char)]

        case "max", "min":
            let a = try evaluate(node.arguments[0])
            let b = try evaluate(node.arguments[1])
            if case .int(let x) = a, case .int(let y) = b {
                return [.int(node.callee == "max" ? Swift.max(x, y) : Swift.min(x, y))]
            }
            let x = try number(a, line), y = try number(b, line)
            return [.real(node.callee == "max" ? Swift.max(x, y) : Swift.min(x, y))]

        case "abs":
            let a = try evaluate(node.arguments[0])
            if case .int(let x) = a {
                // Swift's abs() traps on Int32.min, whose magnitude is one past Int32.max -
                // the same uncatchable class the rest of this file guards against. Negating
                // through subtractingReportingOverflow reports it as an ordinary error.
                if x >= 0 { return [.int(x)] }
                let (negated, overflow) = Int32(0).subtractingReportingOverflow(x)
                guard !overflow else {
                    throw RuntimeError(message: "abs(\(x)) overflows int - use real", line: line)
                }
                return [.int(negated)]
            }
            return [.real(Swift.abs(try number(a, line)))]

        default:
            break
        }

        let unary: [String: (Double) -> Double] = [
            "sqrt": sqrt, "log": log, "exp": exp,
            "sin": sin, "cos": cos, "tan": tan,
            "asin": asin, "acos": acos, "atan": atan,
            "round": { $0.rounded(.toNearestOrAwayFromZero) },
            "ceil": ceil, "floor": floor, "trunc": trunc,
            "sinh": sinh, "cosh": cosh, "tanh": tanh,
            "log10": log10, "log2": log2, "cbrt": cbrt
        ]
        if let function = unary[node.callee] {
            return [.real(function(try number(try evaluate(node.arguments[0]), line)))]
        }

        let binary: [String: (Double, Double) -> Double] =
            ["atan2": atan2, "pow": pow, "hypot": hypot, "fmod": fmod]
        if let function = binary[node.callee] {
            let a = try number(try evaluate(node.arguments[0]), line)
            let b = try number(try evaluate(node.arguments[1]), line)
            return [.real(function(a, b))]
        }

        return nil
    }
}
