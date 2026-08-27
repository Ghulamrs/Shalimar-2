//
//  scanlayout.swift
//  Shalimar scan-layout tests
//
//  Exercises Shalimar/ScanLayout.swift, which rebuilds source lines from OCR bounding boxes.
//  ScanLayout deliberately avoids UIKit and Vision so this can run as a plain command-line binary,
//  the same way the language core does - Tests/regression.sh compiles and runs both.
//
//  Boxes are Vision-normalized: origin bottom-left, so a *larger* y is higher on the page.
//

import Foundation
import CoreGraphics

var pass = 0
var fail = 0

func check(_ name: String, _ got: [String], _ want: [String]) {
    if got == want {
        pass += 1
    } else {
        fail += 1
        print("FAIL: \(name)")
        print("      want: \(want)")
        print("      got:  \(got)")
    }
}

func checkInts(_ name: String, _ got: [Int], _ want: [Int]) {
    if got == want {
        pass += 1
    } else {
        fail += 1
        print("FAIL: \(name)")
        print("      want: \(want)")
        print("      got:  \(got)")
    }
}

// A fragment on row `row` (0 = top), starting at character column `col`, `chars` long.
// One character is 0.01 wide, one line 0.04 tall, so rows are comfortably separated.
func frag(_ text: String, row: Int, col: Int) -> ScanLayout.Fragment {
    let height: CGFloat = 0.04
    let y = 1.0 - (CGFloat(row) + 1) * height
    return ScanLayout.Fragment(
        text: text,
        box: CGRect(x: CGFloat(col) * 0.01, y: y,
                    width: CGFloat(text.count) * 0.01, height: height))
}

// ---------------------------------------------------------------- ordering and grouping
check("reading order is top to bottom",
      ScanLayout.lines(from: [frag("c", row: 2, col: 0),
                              frag("a", row: 0, col: 0),
                              frag("b", row: 1, col: 0)]),
      ["a", "b", "c"])

// The case the rule made load-bearing: one printed line split at a wide gap must come back as one
// line, so the "?" stays mid-line and the parser rejects it. Two lines here would silently produce
// a valid program that the paper original is not.
check("split line is rejoined",
      ScanLayout.lines(from: [frag("x : 1", row: 0, col: 0),
                              frag("? x", row: 0, col: 20)]),
      ["x : 1 ? x"])

check("fragments of a row are ordered left to right",
      ScanLayout.lines(from: [frag("k", row: 0, col: 30),
                              frag("j", row: 0, col: 10),
                              frag("i", row: 0, col: 0)]),
      ["i j k"])

// Rows an entire line apart must not merge, however the observations arrive.
check("adjacent rows stay separate",
      ScanLayout.lines(from: [frag("? y", row: 1, col: 0),
                              frag("? x", row: 0, col: 0)]),
      ["? x", "? y"])

// Slight vertical jitter within one printed line (taller glyphs, a tilted page) must not split it.
let jittered = [
    ScanLayout.Fragment(text: "a", box: CGRect(x: 0.00, y: 0.500, width: 0.01, height: 0.040)),
    ScanLayout.Fragment(text: "b", box: CGRect(x: 0.20, y: 0.508, width: 0.01, height: 0.036)),
]
check("jitter within a line does not split it", ScanLayout.lines(from: jittered), ["a b"])

// ---------------------------------------------------------------------------- indentation
check("indentation is rebuilt from the left margin",
      ScanLayout.lines(from: [frag("fun <> = main() {", row: 0, col: 0),
                              frag("? x", row: 1, col: 2),
                              frag("}", row: 2, col: 0)]),
      ["fun <> = main() {", "  ? x", "}"])

// Indentation is relative to the leftmost text on the page, not to x = 0.
check("margin offset is not indentation",
      ScanLayout.lines(from: [frag("a", row: 0, col: 10),
                              frag("b", row: 1, col: 12)]),
      ["a", "  b"])

check("empty input", ScanLayout.lines(from: []), [])
check("empty text is dropped",
      ScanLayout.lines(from: [frag("", row: 0, col: 0), frag("a", row: 1, col: 0)]),
      ["a"])

// ------------------------------------------------------------------ late-command detection
checkInts("clean program flags nothing",
          ScanLayout.linesWithLateCommand(in: ["fun <> = main() {", "  ? x", "}"]), [])
checkInts("merged lines are flagged",
          ScanLayout.linesWithLateCommand(in: ["fun <> = main() {", "x : 1 ? x", "}"]), [2])
checkInts("two commands on a line are flagged",
          ScanLayout.linesWithLateCommand(in: ["? x ?? y"]), [1])
checkInts("every offending line is reported",
          ScanLayout.linesWithLateCommand(in: ["x : 1 ? x", "ok", "y : 2 ?? y"]), [1, 3])
// A "?" that is not a command must not raise a false alarm - the user would be sent to look for a
// problem that isn't there, on a line the parser is perfectly happy with.
checkInts("? inside a string is not a command",
          ScanLayout.linesWithLateCommand(in: ["? \"what? really?\""]), [])
checkInts("? inside a comment is not a command",
          ScanLayout.linesWithLateCommand(in: ["x : 1 // why? because"]), [])
checkInts("indented command is not late",
          ScanLayout.linesWithLateCommand(in: ["      ?? x"]), [])

print("scan layout: PASS: \(pass)   FAIL: \(fail)")
exit(fail == 0 ? 0 : 1)
