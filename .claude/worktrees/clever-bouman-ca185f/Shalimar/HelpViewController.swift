//
//  HelpViewController.swift
//  Shalimar: G. R. Akhtar
//
//  The language reference, reachable from the "?" in the editor's nav bar.
//
//  Built in code rather than in the storyboard because it is one scrolling text view
//  with no outlets worth wiring, and because the reference text belongs next to the
//  language it documents: when a rule changes, this file is in the same commit.
//
//  Every code line below is real Shalimar and runs as written. Two rules bite hardest
//  when copying from here, so they are stated early and obeyed throughout: a print
//  command must be the first thing on its line, and a declaration must sit at the top
//  of its function rather than inside an if or a loop.
//

import UIKit

final class HelpViewController: UIViewController {

    private let reference = UITextView()

    override func viewDidLoad() {
        super.viewDidLoad()
        title = "Shalimar Reference"

        // Fixed light, matching the editor: the rest of the app pins its colours rather
        // than following the system, so a reference that inverted in dark mode would be
        // the only screen that did.
        view.backgroundColor = UIColor(white: 0.99, alpha: 1)

        reference.text = HelpViewController.text
        reference.font = UIFont.monospacedSystemFont(ofSize: 12, weight: .regular)
        reference.textColor = UIColor(white: 0.12, alpha: 1)
        reference.backgroundColor = view.backgroundColor
        reference.isEditable = false
        reference.isSelectable = true
        reference.alwaysBounceVertical = true
        // Prose in the reference is wrapped to the phone column at source, so nothing
        // reflows mid-sentence; the indented code and tables are all short enough to fit.
        reference.textContainerInset = UIEdgeInsets(top: 12, left: 10, bottom: 24, right: 10)
        // Pinned to the safe area below, so the automatic adjustment would count the nav
        // bar twice and leave the first line clipped underneath it.
        reference.contentInsetAdjustmentBehavior = .never

        reference.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(reference)
        NSLayoutConstraint.activate([
            reference.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor),
            reference.bottomAnchor.constraint(equalTo: view.bottomAnchor),
            reference.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            reference.trailingAnchor.constraint(equalTo: view.trailingAnchor),
        ])
    }

    // Before the first layout, not after: setting the offset in viewDidAppear fought
    // whatever position the text view had already settled into and could leave the
    // reference opening partway down.
    override func viewWillLayoutSubviews() {
        super.viewWillLayoutSubviews()
        guard !hasPlacedAtTop else { return }
        hasPlacedAtTop = true
        reference.setContentOffset(.zero, animated: false)
    }

    private var hasPlacedAtTop = false

    static let text = """
    SHALIMAR - LANGUAGE REFERENCE
    =============================

    A program is one or more functions. Running
    starts at main().

      fun <> = main() {
        ? "Hello world!"
      }

    Press the green arrow to run. Messages
    appear in the console below the editor.

    TWO RULES THAT CATCH EVERYONE
    -----------------------------
    1. A print command opens its line. Nothing
       may come before it on that line.

         ? "ok"     works
         x : 1 ? x  Error: must start its line

    2. Declarations sit at the top of a
       function, never inside an if or a loop.

         fun <> = main() {
           int c : 0
           for i < 3 {
             c : c + i
           }
           ? c
         }

    TYPES
    -----
      int      whole number, -2 to +2 billion
      real     decimal number
      char     one character
      char[]   text, written as char name[size]

    There are no other types. An array of any
    type is written with [] sizes; text is the
    one-dimensional array of char.

    DECLARING
    ---------
      int   n
      int   n : 5
      real  x : 1.5
      char  c
      char  name[20] : "Akhtar"
      real  v[10]
      real  m[3][3]
      int   cube[2][3][5]

    A size must be a whole number, 1 or more. It
    is fixed when the declaration runs.

    A scalar may also be created by assigning to
    it:

      k : 5           k is int
      y : 1.5         y is real

    An array can too, but only from a literal,
    which carries its own shape:

      X : {{0.,1.},{1.,2.}}   a 2x2 real

    Any other right-hand side needs a
    declaration first, because the size cannot
    be known:

      C : A     Declare the array 'C' first

    OMITTED ENTRIES
    ---------------
    A slot left empty stands for zero. The
    commas still fix the shape, so you only type
    the values that matter:

      M : {{1.0,,},{,1.0,},{,,1.0}}
                      the 3x3 identity
      K : {{,,},{,,},{1.,1.,1.}}
                      only the last row set

    A literal is the array's whole new value:
    anything it does not reach becomes zero, not
    what was there before.

    A blank has no type of its own, so one entry
    must carry it. All-blank works only where
    the type is already known:

      real Z[2][2] : {{,},{,}}    zeros
      Z : {{,},{,}}               error

    ASSIGNMENT
    ----------
      x : 5           assign
      x +: 1          add and assign
      x -: 1          subtract and assign
      x = 5           also assigns; prefer ':'

    NUMBERS
    -------
      42              int
      42.             real
      1.5             real
      1e10            real
      1e-20           real
      3.0e8           real

    A number with a point or an exponent is
    real. A real assigned into an int drops the
    fraction.

    PRINTING
    --------
      ?  items        print, then a new line
      ?? items        print, stay on the line

    Items are separated by spaces:

      ? "x is" x "and y is" y

    An array prints as a grid, laid out in
    columns:

      real m[2][2] : {{1.,2.},{3.,4.}}
      ? m

    PRECISION
    ---------
    A real prints to 7 decimal places, 6 inside
    a grid. Change it inside a print command:

      ? prec(12) 1./3.      0.333333333333
      ? prec(2)  1./3.      0.33
      ? prec(-1) 1./3.      back to normal

    prec(n) runs from -1 to 24. It applies from
    that point on, including the rest of its own
    line. -1 restores the default.

    OPERATORS
    ---------
      +  -  *  /  %  ^    arithmetic, ^ power
      =  !=  <  >  <=  >=  comparison
      &  |                 and, or

    Highest first:

      ^
      *  /  %
      +  -
      =  !=  <  >  <=  >=
      &
      |

    The six comparisons all sit at one level, so
    'a <= 3 | a >= 5' needs no brackets. & and |
    take two finished answers and do not stop
    early, so write 'a <= b' rather than
    'a < b | a = b', which works out a twice.

    ^ groups to the right: 2^3^2 is 2^9, not
    8^2. Watch unary minus: -2^2 is 4, not -4.

    Dividing two ints gives an int: 7/2 is 3.
    Use reals for a real answer: 7./2. is 3.5.

    CONDITIONS
    ----------
      if x > 9 {
        ? "big"
      }
      elseif x > 2 {
        ? "middle"
      }
      else {
        ? "small"
      }

    Anything non-zero is true.

    LOOPS
    -----
      while i < 10 {
        i +: 1
      }

      for i : 1 to 10 {
        ?? i
      }

      for i : 10 to 1 step -1 {
        ?? i
      }

      for i < 10 {
        ?? i
      }

    'for i < n' is short for 'for i : 0 to n -
    1', which is the loop that walks an array.
    It takes a step too.

    The counter belongs to the loop and
    disappears after it.

    LEAVING A LOOP EARLY
    --------------------
      for i < s.row {
        if s[i] = char(32) {
          at : i
          break
        }
      }

    'break' leaves the loop. 'continue' takes
    the next pass:

      for i < 5 {
        if i % 2 = 0 {
          continue
        }
        ? i
      }

    prints 1 and 3. In a 'for' the counter still
    advances - the step belongs to the loop, not
    to the body.

    Both bind to the innermost loop. An 'if' is
    not a loop, so a break inside one leaves the
    loop around it; outside every loop it is an
    error.

    FUNCTIONS
    ---------
      fun <outputs> = name(inputs) { body }

    Outputs are TYPES, not names:

      fun <int> = square(n: int) {
        return n * n
      }

      fun <int,int> = divide(a: int, b: int) {
        return (a / b, a % b)
      }

    Two or more returned values need
    parentheses, and are received with < >:

      <q,r> : divide(17, 5)

    A function that declares outputs must return
    them on every path.

    main() takes no inputs.

    ARGUMENTS
    ---------
    A single value is passed by copy - changing
    it inside the function does not affect the
    caller.

    An array is passed by reference. The
    function works on the caller's array:

      fun <> = fill(v[]: real) {
        for i < v.row {
          v[i] : 1.0
        }
      }

    This is how a function hands an array back,
    because an array cannot be returned in < >.

    ARRAY SIZE
    ----------
      A.row       size of the first dimension
      A.col       size of the second
      A.dim(n)    size of dimension n, from 0
      len(A)      same as A.row

    A.row is A.dim(0) and A.col is A.dim(1), at
    any number of dimensions. Asking for a
    dimension the array does not have answers
    -1:

      real v[10]
      ? v.row        10
      ? v.col        -1

    These are read from the array itself, so a
    function can measure an array it was given.

    STRINGS
    -------
    Text is char[]. The size is capacity; the
    text inside can be shorter.

      char a[20] : "alice"
      char b[128] : "bob"

      if a < b {
        ? a "comes first"
      }
      ? a + " and " + b
      a +: "!"

    Compare with = != < > <= >=, join with +,
    append with +:. Comparison reads the text,
    not the capacity, so the same name in a
    char[20] and a char[128] is equal.

    Ordering is by character code, so capitals
    come before lower case: "Zoe" is before
    "adam".

    Joining into a fixed array fits that array;
    anything past the end is dropped.

    CHARACTERS
    ----------
    Indexing text gives one char:

      char s[8] : "abc"
      ? s[0]         a

    A char has no arithmetic of its own. Convert
    it:

      ? int(s[0])          97
      ? char(65)           A
      ? char(int(s[0])-32) A

    char(0) is the end-of-text marker, which is
    how the length of the text inside is found.

    CONVERSIONS
    -----------
      int(x)      drop the fraction, toward zero
      real(x)     exact
      char(x)     character of that code, 0-255

    int(2.7) is 2 and int(-2.7) is -2. It never
    rounds.

    BUILT-IN FUNCTIONS
    ------------------
      abs(x)      sqrt(x)     hypot(x,y)
      log(x)      exp(x)
      sin(x)      cos(x)      tan(x)
      asin(x)     acos(x)     atan(x)
      atan2(y,x)  pow(x,y)
      round(x)    ceil(x)     floor(x)
      trunc(x)
      max(a,b)    min(a,b)
      len(A)
      int(x)      real(x)     char(x)

    Angles are in radians.

    hypot(x,y) is sqrt(x*x + y*y) without the
    overflow. exp(x) is the inverse of log(x).

    trunc(x) drops the fraction toward zero like
    int(x), but the result stays real, so it
    holds magnitudes int(x) has to refuse.

      ? trunc(-2.7)          -2.0000000
      ? int(-2.7)            -2

    CONSTANTS
    ---------
      pi          3.141592653589793
      e           2.718281828459045

    pi and e are read-only. Neither can be
    declared, assigned, or used as a name.

      ? sin(90. * pi / 180.)      1.0000000

    COMMENTS
    --------
      // to the end of the line

    GLOBALS
    -------
    A declaration outside any function is a
    global, and every function below it can use
    it:

      int c : 0

      fun <> = count() {
        c : c + 1
      }

    Only below it. Functions may be written in
    any order, but a global declared under a
    function that uses it is not in scope there -
    the program is built from the top down, and
    the message names the line it is declared on.

    MESSAGES
    --------
      Error:    the program will not run
      Warning:  the program runs anyway

    Every message names its line. A warning
    appears for a function that is never called,
    for a name that hides a global, and for a
    loop whose step points away from its end:

      for i : 10 to 1 step 1

    counts up from 10 and stops at 1, so it runs
    no passes at all. Count down with step -1.

    LIMITS
    ------
      int          about -2 to +2 billion
      recursion    256/(inputs+1) per function
      call depth   1024 frames in total
      prec(n)      -1 to 24

    Passing an int limit is an error, never a
    wrong answer that looks right.

    NOT INCLUDED
    ------------
      An array cannot be returned in < >.
      Pass one in and fill it - it is a
      reference.

      There is no array of strings.

      break and continue bind to the innermost
      loop only. There are no labels, so leaving
      two loops at once needs a flag or a return.

      There is no input. A program only prints.

    THE EDITOR
    ----------
    The editor lays code out as you type. Return
    carries the indent onto the next line, and a
    } typed at the head of a line pulls that line
    back one level.

    Pasted code is laid out the same way, at the
    level it lands in - so an example copied from
    this reference arrives in column, even though
    it is indented here to sit inside the text.

    Two spaces stay two spaces. Other apps end a
    sentence for you by turning the second one
    into a full stop, which would leave a . next
    to a name - the opening of .row as far as the
    lexer is concerned. Here the spaces are put
    back.

    WRITING FOR THE CAMERA
    ----------------------
    The scanner reads printed or clearly written
    code. Shalimar is ASCII, so a curly quote, a
    dash that is not a hyphen, or a Cyrillic
    letter that looks Latin are all rejected
    rather than silently accepted - the message
    names the character and its code.

    =============================
    Shalimar 3.0
    (c) 2019-26 G. R. Akhtar, Islamabad
    August 07, 2026
    =============================
    """
}
