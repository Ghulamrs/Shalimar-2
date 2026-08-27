//
//  main.swift
//  Shalimar regression harness
//
//  A command-line driver for the language core, used by Tests/regression.sh.
//  The core (TokenKind/Node/Parse/Check/Interpreter) is pure Foundation with no
//  UIKit dependency, so it compiles and runs outside the app - which is what
//  makes a fast regression suite possible without an Xcode test target.
//
//  Mirrors ComputeViewController.ComputeTapped deliberately: lex, check
//  lexError, parse, check parseError, check, report diagnostics, then run. If
//  that order ever drifts from the app, this harness stops testing what the app
//  actually does.
//
//  The checker is the stage 3.0 added, and it behaves unlike the other two: it
//  does not stop at the first problem, so every diagnostic is printed and only
//  an error prevents the run. That is why warnings appear in the output of
//  programs that still execute.
//
//  Usage:  shalimar <file.shm>
//  Exit:   0 on a clean run, 1 on a lex, parse or check error.
//

import Foundation

guard CommandLine.arguments.count > 1 else {
    FileHandle.standardError.write(Data("usage: shalimar <file.shm>\n".utf8))
    exit(2)
}

let path = CommandLine.arguments[1]
guard let source = try? String(contentsOfFile: path, encoding: .utf8) else {
    FileHandle.standardError.write(Data("cannot read \(path)\n".utf8))
    exit(2)
}

let lexer = Lexer(input: source)
let tokens = lexer.tokenize()
if let lexError = lexer.lexError {
    print(lexError)
    exit(1)
}

let parser = Parser(tokens: tokens)
let ast = parser.parseProgram()
if let parseError = parser.parseError {
    print("\(parseError)")
    exit(1)
}

let checker = Checker(borrowing: parser.borrowed)
let checkedAST = checker.check(ast)
for diagnostic in checker.diagnostics {
    print(diagnostic)
}
if checker.hasErrors {
    exit(1)
}

let interpreter = Interpreter()
interpreter.output = { print($0, terminator: "") }
interpreter.run(checkedAST)
