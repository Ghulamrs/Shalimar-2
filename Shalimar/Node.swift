import Foundation

public protocol Node: CustomStringConvertible {}

public protocol ExprNode: Node {}
public protocol StmtNode: Node { var line: Int { get } }

public indirect enum ShalimarType: Equatable {
    case int
    case real
    case char
    case array(ShalimarType)

    public var rank: Int {
        if case .array(let element) = self { return 1 + element.rank }
        return 0
    }

    public var scalar: ShalimarType {
        if case .array(let element) = self { return element.scalar }
        return self
    }

    public var isReference: Bool { rank > 0 }

    public static var maxRank = Int.max
    public var isWellFormed: Bool {
        if scalar == .char && rank > 1 { return false }
        return rank <= ShalimarType.maxRank
    }
}

extension ShalimarType: CustomStringConvertible {
    public var description: String {
        switch self {
        case .int:  return "int"
        case .real: return "real"
        case .char: return "char"
        case .array(let element): return "\(element)[]"
        }
    }
}

public struct Parameter: Node {
    public let name: String
    public let type: ShalimarType
    public let byReference: Bool

    public var description: String {
        let marker = (byReference && !type.isReference) ? "&" : ""
        let brackets = String(repeating: "[]", count: type.rank)
        return "\(marker)\(name)\(brackets): \(type.scalar)"
    }
}

public struct PrototypeNode: Node {
    public let name: String
    public let outputs: [ShalimarType]
    public let inputs: [Parameter]
    public let line: Int

    public var arity: Int { inputs.count }
    public var returnCount: Int { outputs.count }

    public var description: String {
        let outs = outputs.map { "\($0)" }.joined(separator: ", ")
        let ins = inputs.map { $0.description }.joined(separator: ", ")
        return "PrototypeNode(fun <\(outs)> = \(name)(\(ins)))"
    }
}

public struct FunctionNode: StmtNode {
    public let prototype: PrototypeNode
    public let body: [StmtNode]
    public var line: Int { prototype.line }

    public var description: String {
        return "FunctionNode(\(prototype), body: \(body.count) statements)"
    }
}

public struct CallNode: ExprNode, StmtNode {
    public let callee: String
    public let arguments: [ExprNode]
    public let line: Int

    public var description: String {
        return "CallNode(callee: \(callee), arguments: \(arguments))"
    }
}

public struct ReturnNode: StmtNode {
    public let prototype: PrototypeNode
    public let exprs: [ExprNode]
    public let line: Int

    public var isBalanced: Bool {
        exprs.count == prototype.returnCount
    }

    public var description: String {
        return "ReturnNode(\(exprs.count) of \(prototype.returnCount) for \(prototype.name))"
    }
}

/// `break` - leave the innermost enclosing loop. The parser refuses one outside a
/// loop, so nothing downstream has to consider a break arriving at function level.
public struct BreakNode: StmtNode {
    public let line: Int
    public var description: String { "BreakNode" }
}

/// `continue` - abandon this iteration and take the next. In a `for` the counter
/// still advances, because the step belongs to the loop rather than to the body.
public struct ContinueNode: StmtNode {
    public let line: Int
    public var description: String { "ContinueNode" }
}

public struct IntNode: ExprNode {
    public let value: Int
    public var description: String { "IntNode(\(value))" }
}

public struct RealNode: ExprNode {
    public let value: Double
    public var description: String { "RealNode(\(value))" }
}

public struct StringNode: ExprNode {
    public let value: String
    public var description: String { "StringNode(\"\(value)\")" }
}

public struct ArrayLiteralNode: ExprNode {
    public let elements: [ExprNode]
    public var description: String {
        return "ArrayLiteralNode(\(elements))"
    }
}

/// An omitted slot in an array literal: the gap in `{1.0,,}`. It holds the place so the
/// commas still describe the shape, and the checker replaces it with the zero of the
/// element type - so nothing downstream of Check ever sees one.
public struct BlankNode: ExprNode {
    public var description: String { "BlankNode" }
}

public struct VariableNode: ExprNode {
    public let name: String
    public var description: String { "VariableNode(\(name))" }
}

public struct IndexNode: ExprNode {
    public let base: ExprNode
    public let index: ExprNode
    public var description: String { "IndexNode(\(base)[\(index)])" }
}

// The dimensions of an array: 'A.row' is axis 0, 'A.col' is axis 1, 'A.dim(n)' is axis n.
// One node for all three because they ask the same question - .row and .col are just
// the two axes a matrix uses often enough to deserve names. 'spelling' is carried only
// so a diagnostic can quote back what the programmer actually wrote.
public struct DimNode: ExprNode {
    public let base: ExprNode
    public let axis: ExprNode
    public let spelling: String

    public var description: String { "DimNode(\(base).\(spelling) axis \(axis))" }
}

// '? prec(10)' - a print-list directive, not a value. It sets how many decimal places a
// real prints to from that point on and contributes nothing to the output itself, so it
// can lead a line ('? prec(10) x') or stand alone ('? prec(10)'). A negative argument
// restores the built-in default.
public struct PrecisionNode: ExprNode {
    public let places: ExprNode
    public var description: String { "PrecisionNode(prec(\(places)))" }
}

public struct ConvertNode: ExprNode {
    public let expr: ExprNode
    public let to: ShalimarType
    public var description: String { "ConvertNode(\(expr) -> \(to))" }
}

public struct BinaryOpNode: ExprNode {
    public enum Op: String {
        case add = "+", subtract = "-", multiply = "*", divide = "/", modulus = "%", power = "^"
        case equal = "=", notEqual = "!=", less = "<", greater = ">"
        case lessEqual = "<=", greaterEqual = ">="
        case and = "&", or = "|"
    }
    public let op: Op
    public let lhs: ExprNode
    public let rhs: ExprNode
    public var description: String { "BinaryOpNode(\(op.rawValue), lhs: \(lhs), rhs: \(rhs))" }
}

extension ExprNode {
    public var isAddressable: Bool {
        return self is VariableNode || self is IndexNode
    }
}

public indirect enum LValue {
    case variable(String)
    case element(LValue, index: ExprNode)

    public var root: String {
        switch self {
        case .variable(let name): return name
        case .element(let base, _): return base.root
        }
    }
}

public struct AssignNode: StmtNode {
    public enum Op: String { case colon = ":", equals = "=" }
    public let target: LValue
    public let op: Op
    public let expr: ExprNode
    public let line: Int
    public var description: String { "AssignNode(\(target) \(op.rawValue) \(expr))" }
}

public struct CompoundAssignNode: StmtNode {
    public enum Op: String { case add = "+:", subtract = "-:" }
    public let target: LValue
    public let op: Op
    public let expr: ExprNode
    public let line: Int
    public var description: String { "CompoundAssignNode(\(target) \(op.rawValue) \(expr))" }
}

public struct MultiAssignNode: StmtNode {
    public let targets: [LValue]
    public let call: CallNode
    public let line: Int
    public var description: String { "MultiAssignNode(\(targets.count) targets : \(call))" }
}

public struct PrintNode: StmtNode {
    public let items: [ExprNode]
    public let newline: Bool
    public let line: Int
    public var description: String { "PrintNode(\(newline ? "?" : "??"), items: \(items))" }
}

public struct IfNode: StmtNode {
    public struct Branch {
        public let condition: ExprNode
        public let body: [StmtNode]
    }
    public let branches: [Branch]
    public let elseBody: [StmtNode]?
    public let line: Int
    public var description: String {
        "IfNode(branches: \(branches.count), else: \(elseBody != nil))"
    }
}

public struct WhileNode: StmtNode {
    public let condition: ExprNode
    public let body: [StmtNode]
    public let line: Int
    public var description: String { "WhileNode(condition: \(condition))" }
}

public struct ForNode: StmtNode {
    public let variable: String
    public let start: ExprNode
    public let end: ExprNode
    public let step: ExprNode?
    public let body: [StmtNode]
    public let line: Int
    public var description: String {
        "ForNode(\(variable) : \(start) to \(end)\(step.map { " step \($0)" } ?? ""))"
    }
}

public struct DeclareNode: StmtNode {
    public let type: ShalimarType
    public let name: String
    public let sizes: [ExprNode]
    public let initial: ExprNode?
    public let line: Int
    public var description: String {
        "DeclareNode(\(type) \(name)\(sizes.isEmpty ? "" : "\(sizes)")"
            + (initial.map { " : \($0)" } ?? "") + ")"
    }
}
