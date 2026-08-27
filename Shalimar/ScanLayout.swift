//
//  ScanLayout.swift
//  Shalimar
//
//  Rebuilding source lines from OCR text observations.
//
//  Vision returns a bag of text regions with bounding boxes, in no guaranteed order and with no
//  notion of a "line". Turning that back into source used to be a sort by vertical position, one
//  observation per line. That was tolerable when Shalimar ignored newlines entirely; it is not now
//  that a print command must be the first token on its line (SHALIMAR_LANGUAGE.md §5.10), because
//  whether two pieces of text share a line decides whether the program is valid.
//
//  The specific hazard is a *split*: Vision emits separate observations either side of a wide gap,
//  so one printed line like `x : 1        ? x` arrives as two regions. Treating those as two lines
//  turns a program the scanner should reject into one that silently runs - the worst outcome, since
//  nothing on screen says the scan changed the meaning. Grouping by vertical overlap puts them back
//  on one line, where the parser reports the error the paper program actually has.
//
//  Deliberately free of UIKit and Vision so it compiles and is tested outside the app - see
//  Tests/scanlayout/main.swift. `ComputeViewController` does the Vision-to-Fragment mapping.
//

import Foundation
import CoreGraphics

enum ScanLayout {
    /// One recognized text region. `box` is Vision's normalized bounding box: 0...1 in both axes,
    /// origin at the bottom-left, so larger `midY` means higher up the page.
    struct Fragment {
        let text: String
        let box: CGRect

        init(text: String, box: CGRect) {
            self.text = text
            self.box = box
        }
    }

    /// Rebuilds source lines, top to bottom, re-indented to match the page.
    static func lines(from fragments: [Fragment]) -> [String] {
        let usable = fragments.filter { !$0.text.isEmpty }
        guard !usable.isEmpty else { return [] }

        let rows = groupIntoRows(usable)

        // Indentation is measured in characters, so it needs a character width. Averaging
        // width/count over every fragment is stable enough for monospaced-ish printed code and
        // costs nothing; a single short fragment would otherwise skew it badly.
        let leftMargin = usable.map { $0.box.minX }.min() ?? 0
        let charWidths = usable.compactMap { fragment -> CGFloat? in
            let count = CGFloat(fragment.text.count)
            return count > 0 ? fragment.box.width / count : nil
        }
        let avgCharWidth = charWidths.isEmpty
            ? 0
            : charWidths.reduce(0, +) / CGFloat(charWidths.count)

        return rows.map { row in
            // Fragments of one line are joined with a single space. Vision splits at visual gaps,
            // never mid-token, so this cannot fuse two identifiers into one - and Shalimar treats
            // any run of whitespace the same way, so the exact width doesn't matter past the
            // indentation that has already been captured.
            let text = row.map(\.text).joined(separator: " ")
            let rowLeft = row.first?.box.minX ?? leftMargin
            let indent = avgCharWidth > 0
                ? max(0, Int(((rowLeft - leftMargin) / avgCharWidth).rounded()))
                : 0
            return String(repeating: " ", count: indent) + text
        }
    }

    /// Groups fragments into visual rows, ordered top to bottom, each ordered left to right.
    private static func groupIntoRows(_ fragments: [Fragment]) -> [[Fragment]] {
        let topDown = fragments.sorted { $0.box.midY > $1.box.midY }

        var rows: [[Fragment]] = []
        for fragment in topDown {
            // Same row when the vertical centres are closer than half a line height. Comparing
            // centres rather than edges tolerates the small box-height differences between a
            // fragment of tall glyphs ("?fj") and one of short ones ("xnm"), which comparing
            // minY/maxY does not.
            if let anchor = rows.last?.first,
               abs(fragment.box.midY - anchor.box.midY) < 0.5 * max(fragment.box.height, anchor.box.height) {
                rows[rows.count - 1].append(fragment)
            } else {
                rows.append([fragment])
            }
        }

        return rows.map { $0.sorted { $0.box.minX < $1.box.minX } }
    }

    /// 1-based numbers of lines holding a `?`/`??` that isn't the line's first token — the shape a
    /// scan produces when Vision reads two printed lines as one region, which no amount of
    /// regrouping can undo. Splitting such a line automatically is not an option: `? x ? y` typed
    /// deliberately on one line must stay the error §5.10 says it is. So the scan reports the
    /// suspicion and leaves the text alone.
    static func linesWithLateCommand(in lines: [String]) -> [Int] {
        var flagged: [Int] = []

        for (offset, line) in lines.enumerated() {
            var inString = false
            var seenToken = false
            var index = line.startIndex

            while index < line.endIndex {
                let char = line[index]
                let next = line.index(after: index)

                if inString {
                    if char == "\"" { inString = false }
                    index = next
                    continue
                }

                if char == "\"" {
                    inString = true
                    seenToken = true
                } else if char == "/", next < line.endIndex, line[next] == "/" {
                    break // rest of the line is a comment; a "?" in prose is not a command
                } else if char == "?" {
                    if seenToken {
                        flagged.append(offset + 1)
                        break
                    }
                    seenToken = true
                    if next < line.endIndex, line[next] == "?" { index = next } // "??" is one command
                } else if !char.isWhitespace {
                    seenToken = true
                }

                index = line.index(after: index)
            }
        }

        return flagged
    }
}
