#!/usr/bin/env python3
"""Regenerate Tools/scan-sheets.html from the programs in Examples/.

The sheets exist to be photographed and scanned back in by the app, so every line on them -
including each program's name and its expected result - is real Shalimar source, written as a "//"
comment. Generating the page from the files rather than maintaining both means the printed program
and the committed program cannot drift apart, which would be a miserable thing to debug from a
photograph.

    python3 Tools/build-sheets.py
"""

import html
import os

ORDER = ["gcd.shm", "quadratic.shm", "factorial.shm", "fibonacci.shm", "table.shm", "prime.shm"]

HEAD = '''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Shalimar scan sheets</title>
<style>
  /* Deliberately light-only and un-styled: this page exists to be photographed, so it ignores
     the viewer's dark theme. Vision reads dark-on-light far better than the reverse, and a dark
     screen photographs with far more glare. No syntax colouring either - coloured tokens are
     lower contrast than plain black, and contrast is the whole game here.

     GENERATED from Examples/*.shm by Tools/build-sheets.py - edit the .shm files, not this. */
  :root { color-scheme: light; }

  * { box-sizing: border-box; }

  body {
    margin: 0;
    background: #ffffff;
    color: #000000;
    font-family: "SF Mono", Menlo, Consolas, "DejaVu Sans Mono", monospace;
  }

  .sheet {
    min-height: 100vh;
    padding: 4vh 5vw;
    display: flex;
    flex-direction: column;
    justify-content: center;
    border-bottom: 1px dashed #cccccc;
  }

  /* Every line on the sheet is real Shalimar source, including the name and the expected result:
     both are "//" comments. Two independent things then protect them. The scanner crops to the
     main() block, so they are dropped before the text reaches the editor - and if a shot misses
     the crop anchors, they still lex as comments rather than as code. */
  pre {
    margin: 0;
    font-size: clamp(19px, 2.4vw, 32px);
    /* Generous leading is the one setting that matters most: tightly-spaced lines are what make
       Vision merge two of them into a single region, and a merged line is the one scanning
       mistake the app can only report, never repair. */
    line-height: 1.85;
    letter-spacing: 0.02em;
    font-variant-ligatures: none;
    white-space: pre;
    overflow-x: auto;
    -webkit-font-smoothing: antialiased;
  }

  .tips {
    padding: 6vh 5vw;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    font-size: clamp(14px, 1.5vw, 18px);
    line-height: 1.7;
    color: #333333;
    max-width: 46em;
  }
  .tips h2 { font-size: 1.3em; margin: 0 0 0.6em; }
  .tips li { margin-bottom: 0.5em; }
  .tips code { font-family: "SF Mono", Menlo, Consolas, monospace; background: #f2f2f2; padding: 0.1em 0.3em; }

  @media print {
    .sheet { page-break-after: always; border-bottom: none; min-height: auto; }
    .tips { page-break-before: always; }
  }
</style>
</head>
<body>
'''

TAIL = '''
<div class="tips">
  <h2>Notes for the capture</h2>
  <ul>
    <li><strong>Both crop anchors must be in frame:</strong> the <code>fun &lt;&gt; = main() {</code>
      line and the <code>}</code> that closes it. The scanner crops the photo to that block; with no
      <code>main(</code> line found it keeps the raw text instead, and the app then reports
      <code>No main() function defined</code> rather than anything about a bad crop.</li>
    <li>Every line here is valid source. The name and expected-result lines are <code>//</code>
      comments, so they are harmless whether they are cropped away or scanned in.</li>
    <li>Each sheet is one full screen. Shoot straight on &mdash; a tilted photo tilts the bounding
      boxes, which is what pushes two lines into one region.</li>
    <li>Every program is entirely inside <code>main()</code>, on purpose: a helper function defined
      outside it would be dropped by the crop.</li>
    <li>Every <code>?</code> and <code>??</code> starts its line, one per line. If a scan merges two
      lines, the alert after scanning names the first offending line.</li>
    <li>Don't zoom so far that a line wraps. A wrapped line photographs as two lines and is read as
      two.</li>
  </ul>
</div>

</body>
</html>
'''


def main():
    # Examples/ holds only shippable programs now - this tool lives beside the page it
    # writes, one directory over.
    here = os.path.dirname(os.path.abspath(__file__))
    examples = os.path.join(os.path.dirname(here), "Examples")
    sheets = []
    for name in ORDER:
        with open(os.path.join(examples, name)) as handle:
            source = handle.read().rstrip("\n")
        sheets.append('<section class="sheet">\n<pre>%s</pre>\n</section>\n' % html.escape(source))

    out = os.path.join(here, "scan-sheets.html")
    with open(out, "w") as handle:
        handle.write(HEAD + "\n".join(sheets) + TAIL)
    print("regenerated %s from %d programs" % (out, len(ORDER)))


if __name__ == "__main__":
    main()
