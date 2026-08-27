#!/usr/bin/env bash
#
# Regression suite for the Shalimar language core.
#
# Compiles TokenKind/Node/Parse/Check/Interpreter plus Tests/harness/main.swift into a
# command-line binary and runs every program below through it, asserting that
# the output contains an expected fragment. The core is pure Foundation, so this
# needs no simulator and no Xcode test target - it runs in a couple of seconds.
#
#   ./Tests/regression.sh
#
# Exits non-zero if any case fails, so it drops straight into a git hook or CI.
#
# Expectations are substring matches, which keeps them readable and tolerant of
# the trailing space the print statements emit after each item.
#
# ---------------------------------------------------------------------------------
# SPEC STATUS: SHALIMAR_LANGUAGE.md describes the 3.0 language and is the authority.
# Every case here traces to a claim in that document, and when a case and the document
# disagree, the document wins and the interpreter is what gets fixed - not the case.
# This file used to carry the opposite note, from the period when the spec still
# described 2.x and could not arbitrate; it was rewritten in cff6505 and can.
# Where a case encodes a deliberate 3.0 change, the comment says which 2.x rule it
# replaced.
# ---------------------------------------------------------------------------------
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/Shalimar"
BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT

swiftc -O "$SRC/TokenKind.swift" "$SRC/Node.swift" "$SRC/Parse.swift" \
          "$SRC/Check.swift" "$SRC/Interpreter.swift" \
          "$ROOT/Tests/harness/main.swift" -o "$BUILD/shalimar" || {
    echo "FATAL: harness failed to compile"
    exit 1
}

pass=0
fail=0
failures=()

# t <name> <expected substring> <source>
t() {
    printf '%s' "$3" > "$BUILD/case.shm"
    local out
    # alarm guards against a genuine infinite loop in the interpreter turning a
    # failing case into a hung suite.
    out=$(perl -e 'alarm 10; exec @ARGV' "$BUILD/shalimar" "$BUILD/case.shm" 2>&1 | tr '\n' ' ')
    if [[ "$out" == *"$2"* ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        failures+=("$1"$'\n'"      want: $2"$'\n'"      got:  $out")
    fi
}

# ---------------------------------------------------------------- 5.5 definitions
# 3.0: the <> list holds output *types*, not output *names*. 2.x named a variable
# there and let the function fall off its end with whatever that variable held; a
# function that declares outputs must now actually return them.
t "typed output list" "1 2 3" 'fun <int,int,int> = f() { return (1,2,3) }
fun <> = main() { <a,b,c> : f()
? a b c }'
t "multi-value return needs parens" "1 2" 'fun <int,int> = f() { return (1,2) }
fun <> = main() { <a,b> : f()
? a b }'
t "return arity must match outputs" "'f' returns 0 values, not 3" 'fun <> = f() { return (1,2,3) }
fun <> = main() { f()
? 1 }'
t "declared output needs a return" "can finish without a return" 'fun <int> = f() { int y : 9 }
fun <> = main() {
? f() }'
t "fresh local scope" "Undefined variable 'k'" 'fun <int> = f() { return k }
fun <> = main() { k : 5
? f() }'
t "main() may not take inputs" "main() must take no inputs" 'fun <> = main(n: int) {
? 1 }'
t "duplicate definition" "already defined" 'fun <> = g() { return 1 }
fun <> = g() { return 2 }
fun <> = main() {
? g() }'
t "unused function warns" "is never called" 'fun <int> = unused() { return 1 }
fun <> = main() {
? 42 }'

# --------------------------------------------------------------- 5.6 calls / args
# 3.1: a user function may take a built-in's name and wins. 3.0 refused the
# definition outright, which cost every program the words max, min, len and abs;
# 2.x accepted it and then called the built-in, so the definition was dead code
# with no warning. Neither. The file's own sqrt is the one that runs.
t "user fn may reuse a builtin name and wins" "999" 'fun <real> = sqrt(x: real) { return 999. }
fun <> = main() {
? sqrt(16.) }'
t "the builtin is there when nobody claims it" "4.0000000" 'uses sqrt
fun <> = main() {
? sqrt(16.) }'
t "builtin rejects string arg" "Cannot use char[] where real is required" 'uses sqrt
fun <> = main() {
? sqrt("hi") }'
# Under-supplying a builtin used to trap on args[0] and take the whole app down
# (SIGTRAP, uncatchable, no diagnostic). 3.0 catches it a stage earlier still - the
# checker refuses the program, so nothing runs at all.
t "builtin too few args, 1-arity" "'sqrt' takes 1, got 0" 'uses sqrt
fun <> = main() {
? sqrt() }'
t "builtin too few args, 2-arity" "'pow' takes 2, got 1" 'uses pow
fun <> = main() {
? pow(2.) }'
t "builtin too few args, zero-arity call" "'atan2' takes 2, got 0" 'uses atan2
fun <> = main() {
? atan2() }'
t "builtin under-supply is a check error" "'min' takes 2, got 1" 'uses min
fun <> = main() {
? "before"
? min(1.) }'
# 3.0: surplus arguments are an error. 2.x warned and dropped them, which meant a
# call with the arguments in the wrong order still ran and returned a wrong number.
t "builtin extra args rejected" "'sqrt' takes 1, got 2" 'uses sqrt
fun <> = main() {
? sqrt(16.,99.) }'
t "too few args errors" "'f' takes 2, got 1" 'fun <real> = f(a: real, b: real) { return a+b }
fun <> = main() {
? f(1.) }'
t "extra args rejected" "'f' takes 1, got 2" 'fun <int> = f(a: int) { return a }
fun <> = main() {
? f(1,2) }'
t "zero args supplied" "'f' takes 1, got 0" 'fun <int> = f(a: int) { return a }
fun <> = main() {
? f() }'
t "expression as argument" "9" 'fun <int> = f(a: int) { return a }
fun <> = main() {
? f(4+5) }'
t "nested call as argument" "5" 'fun <int> = f(a: int) { return a }
fun <> = main() {
? f(f(5)) }'
# A scalar may still be passed by reference with '&', which is a copy-in/copy-back:
# the callee works on its own box and the value is written to the caller's variable
# when the call returns. Untested until now, and the only feature with no coverage.
t "& passes a scalar by reference" "6" 'fun <> = bump(&n: int) {
n : n + 1
}
fun <> = main() { x : 5
bump(x)
? x }'
t "& writes back to an element" "1  3  3" 'fun <> = bump(&n: int) {
n : n + 1
}
fun <> = main() { int v[3] : {1,2,3}
bump(v[1])
? v }'
t "& needs a variable, not a value" "needs a variable" 'fun <> = bump(&n: int) {
n : n + 1
}
fun <> = main() {
bump(1+2) }'
t "a reference is never converted" "must be int[]" 'fun <> = f(v[]: int) {
? v }
fun <> = main() { real A[2] : {1.,2.}
f(A) }'
t "scalar arguments are by value" "1" 'fun <int> = f(a: int) { a : 99 return 0 }
fun <> = main() { x : 1 f(x)
? x }'
t "recursion" "120" 'fun <int> = fact(n: int) { if n < 2 { return 1 } return n*fact(n-1) }
fun <> = main() {
? fact(5) }'

# Recursion depth: unbounded recursion overflows the native stack (SIGSEGV, uncatchable,
# no output). Per-function cap is 256/(inputs+1); a total-frames backstop covers mutual
# recursion, where no single function ever approaches its own cap.
t "1-arg recursion at limit ok" "0" 'fun <int> = d(n: int) { if n < 1 { return 0 } return d(n-1) }
fun <> = main() {
? d(127) }'
t "1-arg recursion over limit" "Recursion too deep in 'd' (limit 128)" 'fun <int> = d(n: int) { if n < 1 { return 0 } return d(n-1) }
fun <> = main() {
? d(128) }'
t "0-arg recursion limit 256" "Recursion too deep in 'z' (limit 256)" 'fun <int> = z() { return z() }
fun <> = main() {
? z() }'
t "3-arg recursion limit 64" "Recursion too deep in 'w' (limit 64)" 'fun <int> = w(a: int, b: int, c: int) { return w(a,b,c) }
fun <> = main() {
? w(1,2,3) }'
t "sequential calls unaffected" "2000" 'fun <int> = f(n: int) { return 1 }
fun <> = main() { s : 0 for i:1 to 2000 { s +: f(i) }
? s }'
t "mutual recursion backstop" "Call stack too deep (limit 1024)" 'fun <int> = a() { return b() }
fun <int> = b() { return c() }
fun <int> = c() { return d() }
fun <int> = d() { return e() }
fun <int> = e() { return a() }
fun <> = main() {
? a() }'
t "runaway recursion reports, not crashes" "before  Error: line 1: Recursion too deep" 'fun <int> = d(n: int) { return d(n+1) }
fun <> = main() {
? "before"
? d(1) }'
# Depth is per-frame, not a lifetime tally: the counter must come back down on the throwing
# path too, or an earlier caught-and-reported failure would poison every later call.
t "depth unwinds after return" "0  0" 'fun <int> = d(n: int) { if n < 1 { return 0 } return d(n-1) }
fun <> = main() {
? d(120)
? d(120) }'
t "multi-argument call" "7" 'fun <int> = f(a: int, b: int) { return a+b }
fun <> = main() {
? f(3,4) }'

# ------------------------------------------- 7.5.1 uses - borrowing a library function
# A library function is not available by being known; it is available by being asked
# for. Before this, all twenty were in scope in every program and cost every program
# their names. `uses sqrt` buys one name for one file, and a file that borrows nothing
# may call its own variable sqrt, fmod or len.
#
# shc gates the same way - ../Compiler-S/docs/FOREIGN.md - and the two must agree on
# which programs are legal, because the differential suite over there records THIS
# interpreter's answers as the expected ones. A disagreement here is not caught there;
# it is certified there.
t "uses grants the call" "4.0000000" 'uses sqrt
fun <> = main() {
? sqrt(16.) }'
t "an unborrowed library call is refused" "add 'uses sqrt' above to call it" 'fun <> = main() {
? sqrt(16.) }'
t "len must be borrowed like the rest" "add 'uses len' above to call it" 'fun <> = main() { real A[3]
? len(A) }'
# Not "Unknown function": this app has one file and nowhere else to look, so the reason
# is already known and saying it beats making the reader guess. shc, which does have
# other files to search, reports the name missing like any other - a divergence in
# wording only. Both refuse the same program.
t "a genuinely unknown name still says Unknown" "Unknown function 'wibble'" 'fun <> = main() {
? wibble(1) }'

# Rule 4: borrowing something and never calling it costs nothing and is not an error.
t "borrowed and never called is fine" "1" 'uses sin, cos, tan
fun <> = main() {
? 1 }'
t "several clauses, interleaved with globals" "0.4794255 0.5463025" 'uses sin
real half : 0.5
uses tan
fun <> = main() {
? sin(half) tan(half) }'
t "borrowing the same name twice is fine" "0.0000000" 'uses sin
uses sin
fun <> = main() {
? sin(0.) }'

# Rule 3, which is the whole point of asking: a name nobody borrowed is an ordinary
# identifier, free for a variable.
t "an unborrowed name is an ordinary variable" "2.5000000" 'fun <> = main() {
real fmod : 2.5
? fmod }'
# And your own function still wins over a name you also borrowed.
t "the file own function wins over its borrow" "999" 'uses sqrt
fun <real> = sqrt(x: real) { return 999. }
fun <> = main() {
? sqrt(16.) }'

# The three conversions are not library functions and need no borrow - nor could they
# have one: int is a keyword, and uses takes an identifier, so it cannot be spelt.
t "conversions need no borrow" "2 3.0000000 J" 'fun <> = main() {
? int(2.7) real(3) char(74) }'
t "uses int is not spellable" "needs a library function" 'uses int
fun <> = main() {
? 1 }'

# The boundary is the table, and the table is honest rather than a matter of policy: a
# row exists only where the C signature can be written in int, real, char and arrays of
# them. A name outside it is refused where it is ASKED for, with the reason - not at the
# call, and not left to a link this app does not have.
t "a pointer function is refused by name" "'memset' takes a pointer" 'uses memset
fun <> = main() {
? 1 }'
t "a variadic function is refused by name" "'printf' takes a variable number" 'uses printf
fun <> = main() {
? 1 }'
t "a two-output function is told the Shalimar shape" "two-output 'fun' is the Shalimar shape" 'uses modf
fun <> = main() {
? 1 }'
t "a name in no table at all is refused plainly" "is not a library function Shalimar knows" 'uses wibble
fun <> = main() {
? 1 }'
# At the line the NAME is on, not the line the clause starts on. shc keeps the name's
# own line too, and the two were briefly out by one here.
t "refused at the line the name is on" "line 2: 'memset'" 'uses sin,
memset
fun <> = main() {
? sin(0.) }'
# Checked before the search for main(), so a program missing both still hears about the
# borrow it cannot have rather than only about main.
t "a borrow is checked even with no main" "'printf' takes a variable number" 'uses printf
fun <> = f() {
? 1 }'

# The clause itself, as the parser reads it.
t "uses needs a name" "needs a library function" 'uses
fun <> = main() {
? 1 }'
t "uses needs a name after the comma" "must follow the comma" 'uses sin,
fun <> = main() {
? 1 }'
# The foreign-declaration form is shc's to link and this app's to refuse, by name -
# a stated divergence in ../Compiler-S/docs/CONFORMANCE.md, not a defect. There is no
# link step here and there never will be.
t "a foreign declaration is refused by name" "'c_total' comes from a library" 'uses <real> = c_total(a[]: real)
fun <> = main() {
? 1 }'

# Rule 3: a borrowed name may not also be a variable. `fmod` is an ordinary
# identifier in every file that does not borrow it, and in one that does the name is
# spoken for - `real fmod` beside `fmod(7.5, 2.0)` would be one name meaning two
# things in one file, which is the hazard the language named when it refused `pi : 3`.
#
# Stricter than a constant, which may be had by declaring it: a borrow has already
# claimed the name, so there is no declaring your way out of it. Six places introduce
# a name and all six are guarded.
t "borrowed name as a local" "'fmod' is borrowed on line 1" 'uses fmod
fun <> = main() { real fmod : 2.5
? fmod }'
t "borrowed name as a global" "'fmod' is borrowed on line 1" 'uses fmod
real fmod : 2.5
fun <> = main() {
? fmod }'
t "borrowed name as a parameter" "'fmod' is borrowed on line 1" 'uses fmod
fun <real> = g(fmod: real) { return fmod }
fun <> = main() {
? g(2.5) }'
t "borrowed name by assignment" "'fmod' is borrowed on line 1" 'uses fmod
fun <> = main() { fmod : 2.5
? fmod }'
t "borrowed name as a loop counter" "'fmod' is borrowed on line 1" 'uses fmod
fun <> = main() {
for fmod : 1 to 2 {
? fmod } }'
t "borrowed name as a multi-assign target" "'sqrt' is borrowed on line 1" 'uses sqrt
fun <> = main() {
<sqrt> : sqrt(16.) }'
# The clause is per FILE, so where it sits does not matter - the parser has collected
# every borrow before the checker starts.
t "the uses may sit below the variable" "'fmod' is borrowed on line 3" 'fun <> = main() { real fmod : 2.5
? fmod }
uses fmod'
# One mistake, one message. Refusing the name and then leaving it undefined gave
# "'fmod' is borrowed" followed by "Undefined variable 'fmod'" a line later, pointing
# at something that was not the mistake.
t "the refusal does not cascade" "Undefined variable" 'uses fmod
fun <> = main() { real fmod : 2.5
? fmod
? nowhere }'
# And the whole point of asking: the name is free in a file that never borrowed it.
t "not borrowed, so free for a variable" "2.5000000" 'fun <> = main() { real fmod : 2.5
? fmod }'
t "not borrowed, so free as a counter" "1  2" 'fun <> = main() {
for fmod : 1 to 2 {
? fmod } }'
t "borrowing and only calling is fine" "1.5000000" 'uses fmod
fun <> = main() {
? fmod(7.5, 2.0) }'

# ------------------------------------------------- 7 results / multi-assign arity
# 3.0: mismatched multi-assign arity is a check error. 2.x warned and carried on,
# silently discarding surplus values and leaving surplus variables untouched.
t "plain assign takes first" "25" 'fun <int> = sq(x: int) { return x*x }
fun <> = main() { y : sq(5)
? y }'
t "call inline in expression" "10" 'fun <int> = sq(x: int) { return x*x }
fun <> = main() {
? 1 + sq(3) }'
t "bare call is not a return" "5" 'fun <int> = sq(x: int) { return x*x }
fun <> = main() { sq(4)
? 5 }'
t "multi-assign captures both" "3 9" 'fun <int,int> = p() { return (3,9) }
fun <> = main() { <d,k> : p()
? d k }'
t "too few targets is an error" "returns 2, not 1" 'fun <int,int> = f() { return (1,2) }
fun <> = main() { <a> : f()
? a }'
t "too many targets is an error" "returns 1, not 2" 'fun <int> = f() { return 1 }
fun <> = main() { <a,b> : f()
? a }'
t "bare return from a 0-output fn" "5" 'fun <> = f() { return }
fun <> = main() { f()
? 5 }'
# A builtin has no PrototypeNode, so the arity and define steps used to be skipped
# entirely - '<s> : sqrt(16.)' left s undeclared and the next line called it undefined.
t "builtin single multi-assign" "4.0" 'uses sqrt
fun <> = main() { <s> : sqrt(16.)
? s }'
t "builtin multi-assign arity" "returns 1, not 2" 'uses sqrt
fun <> = main() { <s,t> : sqrt(16.)
? s }'
t "builtin multi-assign into declared" "4.0" 'uses sqrt
fun <> = main() { real s : 0.
<s> : sqrt(16.)
? s }'

# ------------------------------------------------------------ 6 strings are char[]
# 3.0: a string is a char array with a type of its own. 2.x had no string type - a
# string used as a value silently became 0.0, so every one of these ran and produced
# a plausible wrong number instead of a diagnostic.
t "string assigns as char[]" "hello" 'fun <> = main() { x : "hello"
? x }'
t "declared string" "abc" 'fun <> = main() { char s[8] : "abc"
? s }'
t "string in arithmetic is a type error" "needs scalars, got int and char[]" 'fun <> = main() {
? 1 + "a" }'
t "string as a condition is a type error" "Condition cannot be char[]" 'fun <> = main() { if "a" {
? 1 } else {
? 2 } }'
t "string as scalar arg is a type error" "Cannot use char[] where int is required" 'fun <int> = f(a: int) { return a }
fun <> = main() {
? f("hi") }'
t "string prints literally" "ok" 'fun <> = main() {
? "ok" }'
t "non-ASCII allowed in string" "wörld" 'fun <> = main() {
? "héllo wörld" }'

# ------------------------------------------------------------------ pi constant
# 2.x had 'pi' and 'e' and resolved them last, behind locals and globals, so a variable
# of the same name shadowed the constant. 3.0 makes pi genuinely read-only instead: with
# a checker that defines a variable on first assignment, a shadowable constant means
# 'pi : 3' quietly overwrites it rather than shadowing it. One name, one meaning.
t "pi reads at default precision" "3.1415927" 'fun <> = main() {
? pi }'
t "pi carries full precision" "3.141592653589793" 'fun <> = main() {
? prec(15) pi }'
t "pi in arithmetic" "12.5663706" 'fun <> = main() { real r : 2.
? pi * r * r }'
t "degrees to radians" "1.0000000" 'uses sin
fun <> = main() { real d : 90.
? sin(d * pi / 180.) }'
t "pi cannot be assigned" "'pi' is a constant" 'fun <> = main() { pi : 3.
? pi }'
t "pi may be declared, and is then yours" "3.0000000" 'fun <> = main() { real pi : 3.
? pi }'
t "pi may be a global" "3.0000000" 'real pi : 3.
fun <> = main() {
? pi }'
t "pi may be a parameter" "1.0000000" 'fun <> = f(pi: real) {
? pi }
fun <> = main() { f(1.) }'
t "pi cannot be a loop counter" "'pi' is a constant" 'fun <> = main() { for pi < 3 {
?? pi }
? "" }'

# exp/hypot/trunc, added alongside 'e'. trunc is not int(): it truncates the same way
# but stays real, so it handles magnitudes int() has to refuse.
t "exp" "2.7182818" 'uses exp
fun <> = main() {
? exp(1.) }'
t "exp inverts log" "2.5000000" 'uses log, exp
fun <> = main() {
? log(exp(2.5)) }'
t "hypot" "5.0000000" 'uses hypot
fun <> = main() {
? hypot(3.,4.) }'
t "trunc stays real" "2.0000000 -2.0000000" 'uses trunc
fun <> = main() {
? trunc(2.7) trunc(-2.7) }'
t "trunc past int range" "3000000000.0000000" 'uses trunc
fun <> = main() {
? trunc(3000000000.7) }'
t "int() still refuses that" "Cannot convert" 'fun <> = main() {
? int(3000000000.) }'
# abs is generic like max/min now - an int in, an int out. It used to be declared unary,
# so abs(-5) came back 5.0000000 while max(3,4) stayed 4.
t "abs of an int stays int" "5" 'uses abs
fun <> = main() { n : -5
? abs(n) }'
t "abs of a real stays real" "5.5000000" 'uses abs
fun <> = main() {
? abs(-5.5) }'
# Swift's abs() traps on Int32.min, which is the uncatchable class - it must report.
t "abs(Int32.min) reports" "overflows int" 'uses abs
fun <> = main() { n : -2147483647
n : n - 1
? abs(n) }'
# 'e' was withheld through early 3.0, on the reasoning that exponent notation owns that
# letter next to a digit and a bare 'e' beside numbers reads as a typo for 1e5. It is
# restored now that exp() exists: log's inverse wants its constant, and the lexer turns
# out to separate the two cleanly - '? 2 e' is two items, '? 2e5' is one number, and a
# name like e5 is unaffected. The objection was readability, not ambiguity.
t "e is a constant" "2.7182818" 'fun <> = main() {
? e }'
t "e cannot be assigned" "'e' is a constant" 'fun <> = main() { e : 5
? e }'
t "log(e) is 1" "1.0000000" 'uses log
fun <> = main() {
? log(e) }'

# ------------------------------------------- strings: compare, order, concatenate
# Two strings are the one array pairing the operators accept. Comparison reads the text
# up to the terminator and never the declared capacity, so the same name in a char[20]
# and a char[128] is equal - without that, buffer size would leak into meaning.
t "equal across capacities" "equal" 'fun <> = main() { char a[20] : "bob"
char b[128] : "bob"
if a = b {
? "equal" } }'
t "not equal" "different" 'fun <> = main() { char a[20] : "bob"
if a != "alice" {
? "different" } }'
t "ordering" "alice before bob" 'fun <> = main() { char a[20] : "alice"
char b[20] : "bob"
if a < b {
? "alice before bob" } }'
t "a prefix orders first" "ann before anna" 'fun <> = main() { char a[20] : "ann"
if a < "anna" {
? "ann before anna" } }'
# Ordering is by byte, so uppercase sorts before lowercase. Conventional, and the reason
# a name list wants folding to one case first - recorded so it is not a surprise later.
t "ordering is ASCII, not alphabetic" "uppercase first" 'fun <> = main() {
if "Zoe" < "adam" {
? "uppercase first" } }'
t "concatenate two strings" "hello world" 'fun <> = main() { char a[20] : "hello "
char b[20] : "world"
? a + b }'
t "concatenate with a literal" "Dr. Akhtar" 'fun <> = main() { char a[20] : "Dr. "
? a + "Akhtar" }'
t "assign a joined result" "abcd" 'fun <> = main() { char a[8] : "ab"
char c[20]
c : a + "cd"
? c }'
# The result is sized to the text, but landing it in a buffer still fits the buffer.
t "joining truncates to capacity" "abc" 'fun <> = main() { char a[20] : "abcdef"
char small[4]
small : a + "ghi"
? small }'
t "append with +:" "abc" 'fun <> = main() { char s[32] : "a"
s +: "b"
s +: "c"
? s }'
t "build a string in a loop" "xyxyxy" 'fun <> = main() { char s[32] : ""
char d[8] : "xy"
for i < 3 {
s +: d }
? s }'
t "strings are assignable whole" "alice bob" 'fun <> = main() { char a[20] : "bob"
char b[20] : "alice"
char t[20]
t : a
a : b
b : t
? a b }'
t "ordering with <=" "ann first" 'fun <> = main() { char a[20] : "ann"
if a <= "anna" {
? "ann first" } }'
t "<= on equal strings" "same" 'fun <> = main() { char a[20] : "bob"
char b[128] : "bob"
if a <= b {
? "same" } }'
t ">= on equal strings" "same" 'fun <> = main() { char a[20] : "bob"
if a >= "bob" {
? "same" } }'
# Only those seven operators, only on two strings - everything else stays refused.
t "subtraction is refused" "does not apply to strings" 'fun <> = main() { char a[8] : "ab"
? a - "b" }'
t "-: on a string is refused" "'-:' does not apply to strings" 'fun <> = main() { char a[8] : "ab"
a -: "b" }'
t "string plus number is refused" "needs scalars, got char[] and int" 'fun <> = main() { char a[8] : "ab"
? a + 1 }'
t "numeric arrays still refused" "needs scalars, got real[] and real[]" 'fun <> = main() { real v[2] : {1.,2.}
? v + v }'
# A list of names is still not expressible: char is one-dimensional by design.
t "no array of strings" "strings are 1-D" 'fun <> = main() { char names[3][20]
? 1 }'

# --------------------------------------------------------- 2.5 <= and >=
# The two comparisons the language went without. Before they existed the only spelling
# was 'a < b | a = b', which evaluates the left side twice, because '&' and '|' take two
# finished values and do not short-circuit. Examples/invert.shm carried the one instance
# of it in the whole corpus, and is now the '<=' it always meant.
t "int <= is true when equal" "yes" 'fun <> = main() { int a : 3
if a <= 3 {
? "yes" } }'
t "int <= is true when less" "yes" 'fun <> = main() { int a : 2
if a <= 3 {
? "yes" } }'
t "int <= is false when greater" "no" 'fun <> = main() { int a : 4
if a <= 3 {
? "yes" }
if a > 3 {
? "no" } }'
t "int >= is true when equal" "yes" 'fun <> = main() { int a : 3
if a >= 3 {
? "yes" } }'
t "int >= is false when less" "no" 'fun <> = main() { int a : 2
if a >= 3 {
? "yes" }
if a < 3 {
? "no" } }'
t "real <= at the boundary" "yes" 'fun <> = main() { real x : 1.5
if x <= 1.5 {
? "yes" } }'
t "real >= mixes with int" "yes" 'fun <> = main() { real x : 3.0
if x >= 3 {
? "yes" } }'
# The comparison is a value like any other, and its type is int.
t "<= yields an int" "1" 'fun <> = main() { int c : 2 <= 3
? c }'
t ">= yields an int" "0" 'fun <> = main() { int c : 2 >= 3
? c }'
# Precedence 30, the same as the other four, so it binds tighter than '&' and '|' and
# needs no parentheses - this is what let invert.shm write its old form unbracketed.
t "<= binds tighter than |" "yes" 'fun <> = main() { int a : 5
if a <= 3 | a >= 5 {
? "yes" } }'
t "<= binds tighter than &" "yes" 'fun <> = main() { int a : 4
if a >= 3 & a <= 5 {
? "yes" } }'
# Both characters must be adjacent: the token is '<=', not '<' and '=' with a gap.
t "a gap is not the operator" "Unexpected '='" 'fun <> = main() { int a : 3
if a < = 3 {
? "yes" } }'
t "no spaces needed either" "yes" 'fun <> = main() { int a : 3
if a<=3 {
? "yes" } }'
# The header hazard '>=' introduced. 'fun <>= main()' parsed before the operator existed,
# and the lexer now hands that '>=' back as one token, so parseDefinition takes it as the
# closing '>' plus the separator '='. Without this the operator would silently break
# programs that never use it.
t "no-space empty output list" "ok" 'fun <>= main() {
? "ok" }'
t "no-space typed output list" "7" 'fun <int>= f() { return 7 }
fun <> = main() {
? f() }'
t "no-space multi output list" "1 2" 'fun <int,int>= f() { return (1,2) }
fun <> = main() {
<a,b> : f()
? a b }'

# -------------------------------------------------------- 2.2 lexer / ASCII rule
t "underscore identifier" "7" 'fun <> = main() { _foo : 7
? _foo }'
t "digits inside identifier" "3" 'fun <> = main() { x1 : 3
? x1 }'
t "keywords case-insensitive" "yes" 'fun <> = main() { x : 1 IF x > 0 {
? "yes" } }'
t "Cyrillic homoglyph rejected" "U+0445" 'fun <> = main() { хn : 5
? xn }'
t "Greek homoglyph rejected" "U+039F" 'fun <> = main() { Ο : 5
? O }'
t "unknown character rejected" "U+0040" 'fun <> = main() { x @ : 5
? x }'

# ------------------------------------------------------------------- 2.3 numbers
# The '.' of an attribute and the '.' of a numeral are the same character, and the
# numeral rule is tried first, so these guard that adding '.row' did not open a hole
# in number lexing.
t "malformed number rejected" "Malformed number '1.2.3'" 'fun <> = main() {
? 1.2.3 }'
t "malformed number in expression" "Malformed number '0.0.1'" 'fun <> = main() { x : 0.0.1 + 2
? x }'
t "trailing dot is not malformed" "1.0" 'fun <> = main() {
? 1. }'
t "real literal still lexes whole" "1.5000000" 'fun <> = main() {
? 1.5 }'

# ------------------------------------------------------- exponent notation
# '1e-20' beats writing the zeros out. An exponent always makes a real - the magnitudes
# that need one do not fit an int anyway. The exponent digits are scanned loosely so a
# truncated '1e' is reported as a malformed number rather than splitting into the number
# 1 and an identifier 'e', which would desync every token after it.
t "negative exponent" "0.0000000000000000000100" 'fun <> = main() { real tol : 1e-20
? prec(22) tol }'
t "positive exponent" "10000000000.0000000" 'fun <> = main() {
? 1e10 }'
t "exponent with a point" "1500.0000000" 'fun <> = main() {
? 1.5e3 }'
t "explicit plus exponent" "100000.0000000" 'fun <> = main() {
? 1e+5 }'
t "small exponent" "0.2000000" 'fun <> = main() {
? 2e-1 }'
t "exponent then subtraction" "99998.0000000" 'fun <> = main() {
? 1e5-2. }'
t "truncated exponent is malformed" "Malformed number '1e'" 'fun <> = main() {
? 1e }'
t "exponent sign with no digits" "Malformed number '1e-'" 'fun <> = main() { x : 1e-
? x }'
# The exponent must not swallow an ordinary subtraction, or a name that begins with e.
t "plain subtraction unaffected" "1" 'fun <> = main() {
? 2-1 }'
t "a name beginning with e is unaffected" "2 7" 'fun <> = main() { e5 : 7
? 2 e5 }'
t "e beside a number stays two items" "2 2.7182818" 'fun <> = main() {
? 2 e }'

# ------------------------------------------------- ? prec(n) print precision
# A print-list directive, not a value: it prints nothing itself and applies from that
# point on, including the rest of its own line. The argument runs -1 through 24 and
# clamps to that range at both ends, so -1 (or anything below it) restores the default
# and nothing is ever refused for being out of range.
t "prec raises places" "0.333333333333" 'fun <> = main() {
? prec(12) 1./3. }'
t "prec lowers places" "0.33" 'fun <> = main() {
? prec(2) 1./3. }'
t "prec persists to later lines" "0.333  0.333" 'fun <> = main() {
? prec(3) 1./3.
? 1./3. }'
t "prec(-1) restores the default" "0.333  0.3333333" 'fun <> = main() {
? prec(3) 1./3.
? prec(-1) 1./3. }'
t "prec applies to a grid too" "1.000  0.500" 'fun <> = main() { real A[1][2] : {{1.,0.5}}
? prec(3) A }'
t "prec prints nothing itself" "x 1.00" 'fun <> = main() {
? prec(2) "x" 1. }'
t "prec takes an expression" "0.3333" 'fun <> = main() { n : 2
? prec(n*2) 1./3. }'
t "prec(24) is the ceiling" "1.000000000000000000000000" 'fun <> = main() {
? prec(24) 1. }'
t "prec clamps above 24" "1.000000000000000000000000" 'fun <> = main() {
? prec(500) 1. }'
t "prec clamps below -1" "1.000  1.0000000" 'fun <> = main() {
? prec(3) 1.
? prec(0-5) 1. }'
t "prec rejects a real" "prec() needs an int" 'fun <> = main() {
? prec(2.5) 1. }'
# Contextual: only 'prec(' in a print list is the directive.
t "prec is still a usable name" "9" 'fun <> = main() { prec : 9
? prec }'
t "prec outside a print list" "belongs after '?'" 'fun <> = main() { prec(3)
? 1. }'
t "a function named prec is refused" "reserved for '? prec(n)'" 'fun <int> = prec(n: int) { return n }
fun <> = main() {
? 1. }'
t "int and real print differently" "42 42.0" 'fun <> = main() {
? 42 42. }'

# ------------------------------------------------- int() / real() conversions
# Both were implemented in the checker and the interpreter but unreachable: the lexer
# turns 'int' into a type keyword before the parser can read 'int(' as a call, so every
# use was a parse error. real -> int drops the fraction whatever the sign - it is a
# truncation toward zero, never a floor, so -2.7 gives -2 and not -3.
t "int() drops a positive fraction" "2" 'fun <> = main() {
? int(2.7) }'
t "int() drops a negative fraction" "-2" 'fun <> = main() {
? int(0.-2.7) }'
t "int() of an int is identity" "7" 'fun <> = main() {
? int(7) }'
t "real() is exact" "20.0000000" 'fun <> = main() { int i : 20
? real(i) }'
# The declared input types of these two builtins must not coerce the argument: running
# 'real(2.7)' through the int input would truncate the value the call exists to widen.
t "real() does not truncate" "2.7000000" 'fun <> = main() { real x : 2.7
? real(x) }'
t "int() inside arithmetic" "3" 'fun <> = main() { real x : 7.
? int(x/2.) }'
# The gap that had no workaround: a real index is refused outright, and without a
# conversion callable inside the brackets it needed a temporary variable.
t "int() converts an index" "3.0000000" 'fun <> = main() { real A[4] : {1.,2.,3.,4.}
real x : 2.9
? A[int(x)] }'
t "int() in a return" "9" 'fun <int> = f(x: real) { return int(x) }
fun <> = main() {
? f(9.8) }'
t "int() rejects an array" "needs one value, not real[]" 'fun <> = main() { real A[2] : {1.,2.}
? int(A) }'
t "int() rejects a string" "needs one value, not char[]" 'fun <> = main() {
? int("hi") }'
t "int() out of range reports" "Cannot convert" 'uses pow
fun <> = main() {
? int(pow(10.,30.)) }'
t "int() of nan reports" "Cannot convert nan" 'fun <> = main() {
? int(0./0.) }'
# A type keyword still opens a declaration everywhere it did before - only a keyword
# immediately followed by "(" is read as a conversion.
t "declaration still parses" "5 1.5000000" 'fun <> = main() { int k : 5
real r : 1.5
? k r }'
t "declaration after a print" "1  5" 'fun <> = main() { x : 1
? x
int k : 5
? k }'
t "bare int is still an error" "Unexpected" 'fun <> = main() {
? int }'
# int(c) and char(n) are the bridge between a character and its code point. Without
# them nothing character-specific was expressible: a char could be compared to another
# char but never named, so testing for a space or a digit had no spelling at all.
t "int() of a char is its code" "65" 'fun <> = main() { char s[8] : "A"
? int(s[0]) }'
t "char() of a code" "A" 'fun <> = main() {
? char(65) }'
t "conversion round trip" "Q" 'fun <> = main() { char s[8] : "Q"
? char(int(s[0])) }'
t "arithmetic on code points" "A" 'fun <> = main() { char s[8] : "a"
? char(int(s[0]) - 32) }'
t "real() of a char" "65.0000000" 'fun <> = main() { char s[8] : "A"
? real(s[0]) }'
t "char() truncates a real" "A" 'fun <> = main() {
? char(65.9) }'
# char(0) names the terminator, which previously had no spelling but an uninitialised
# variable - that made the end of a string impossible to test for readably.
t "char(0) is the terminator" "terminator found" 'fun <> = main() { char nul : char(0)
char s[8] : "ab"
if s[2] = nul {
? "terminator found" } }'
# A char is one byte, so the narrowing is range-checked rather than wrapped: a wrapped
# code point is a wrong character that looks like a right one.
t "char() rejects 256" "256 is not a char (0 to 255)" 'fun <> = main() {
? char(256) }'
t "char() rejects a negative" "-1 is not a char (0 to 255)" 'fun <> = main() {
? char(0-1) }'
t "char() rejects an array" "needs one value, not char[]" 'fun <> = main() { char s[8] : "ab"
? char(s) }'
t "char declaration untouched" "ab a" 'fun <> = main() { char s[4] : "ab"
char c : s[0]
? s c }'
# The type wall stays: chars convert on request, they never mix into arithmetic.
t "char still rejects arithmetic" "Cannot use int where char is required" 'fun <> = main() { char s[8] : "A"
? s[0] + 1 }'
# What the conversions unlock, end to end - upper() cannot be written without them.
t "upper() over a string" "HELLO" 'fun <int> = length(t[]: char) {
char nul : char(0)
int n : 0
for i < t.row {
if t[i] != nul {
if n = i { n : i + 1 }
}
}
return n
}
fun <> = upper(t[]: char) {
int a : int(char(97))
int c : 0
for i < length(t) {
c : int(t[i])
if c > a - 1 & c < a + 26 {
t[i] : char(c - 32)
}
}
}
fun <> = main() { char s[16] : "hello"
upper(s)
? s }'

# ------------------------------------------------ array dimensions: .row/.col/.dim
# '.row' is axis 0 and '.col' is axis 1 whatever the rank, with '.dim(n)' for the
# rest. An axis the array does not have answers -1 rather than failing, so asking a
# vector for its columns is a legal question with a recognizable answer.
t "matrix row and col" "3 4" 'fun <> = main() { real A[3][4]
? A.row A.col }'
t "dim matches row and col" "3 4" 'fun <> = main() { real A[3][4]
? A.dim(0) A.dim(1) }'
t "rank 3 third axis" "5" 'fun <> = main() { int C[2][3][5]
? C.dim(2) }'
t "row and col work at rank 3" "2 3" 'fun <> = main() { int C[2][3][5]
? C.row C.col }'
t "vector col is -1" "10 -1" 'fun <> = main() { real v[10]
? v.row v.col }'
t "dim past the rank is -1" "-1" 'fun <> = main() { real v[10]
? v.dim(1) }'
t "negative axis is -1" "-1" 'fun <> = main() { real A[2][2]
? A.dim(0-1) }'
t "computed axis" "5" 'fun <> = main() { int C[2][3][5]
? C.dim(1+1) }'
t "attribute chains off a subscript" "4" 'fun <> = main() { real A[3][4]
? A[0].row }'
t "string length is capacity" "8 -1" 'fun <> = main() { char s[8] : "abc"
? s.row s.col }'
t "dimensions of a reference parameter" "3 4" 'fun <> = show(M[][]: real) {
? M.row M.col }
fun <> = main() { real A[3][4]
show(A) }'
t "row agrees with len" "3 3" 'uses len
fun <> = main() { real A[3][4]
? len(A) A.row }'
# The dimensions are measured, not declared, so a row replaced at run time reports its
# real length rather than the one the declaration asked for.
t "dimensions are measured not declared" "2" 'fun <> = main() { real A[2][5]
A[0] : {1.,2.}
? A[0].row }'
t "scalar has no dimensions" "needs an array, not int" 'fun <> = main() { x : 1
? x.row }'
t "unknown attribute rejected" "use .row, .col or .dim(n)" 'fun <> = main() { real A[2]
? A.size }'
t "dim needs an axis" "needs an axis, as .dim(0)" 'fun <> = main() { real A[2]
? A.dim }'
t "axis must be an int" "Axis must be int, not real" 'fun <> = main() { real A[2][2]
? A.dim(1.5) }'
t "attribute is read-only" "is read-only" 'fun <> = main() { real A[2]
A.row : 5 }'
# row/col/dim are recognized only after a dot, so they stay ordinary identifiers.
t "row is still a usable name" "9 2" 'fun <> = main() { real A[2][2]
row : 9
? row A.row }'
t "dim is still a usable name" "7" 'fun <> = main() { dim : 7
? dim }'

# --------------------------------------------------- for i < n  (count shorthand)
# 'for i < n' is sugar for 'for i : 0 to n - 1' and expands to exactly that node,
# which is why 'step' rides along unchanged.
t "shorthand matches long form" "long 0 1 2 short 0 1 2" 'fun <> = main() { real A[2][3]
?? "long"
for i : 0 to A.col - 1 {
?? i }
?? "short"
for i < A.col {
?? i } }'
t "shorthand over rows" "0 1" 'fun <> = main() { real A[2][3]
for i < A.row {
?? i } }'
t "shorthand with step" "0 2 4" 'fun <> = main() { int C[2][3][5]
for k < C.dim(2) step 2 {
?? k } }'
t "shorthand on a plain count" "0 1 2 3" 'fun <> = main() { n : 4
for i < n {
?? i } }'
# v.col is -1 here, so the loop is 0 to -2: it runs zero times rather than failing.
t "shorthand over a missing axis" "none" 'fun <> = main() { real v[4]
for j < v.col {
?? "ran" }
? "none" }'
t "nested shorthand" "0 1 2 10 11 12" 'fun <> = main() { real A[2][3]
for i < A.row {
for j < A.col {
?? i*10+j } } }'
# Because it expands to "0 to n - 1", a real bound subtracts before it widens: this
# stops at 1.0, where reading "i < 2.9" literally would also have given 2.0. Recorded
# rather than silently inherited - a count loop over a real bound is the odd case.
t "real bound follows the expansion" "0.0000000 1.0000000" 'fun <> = main() { for i < 2.9 {
?? i } }'
t "shorthand counter is loop-local" "Undefined variable 'i'" 'fun <> = main() { n : 3
for i < n {
?? n }
? i }'
t "long form still works" "3 2 1 0" 'fun <> = main() { for i : 3 to 0 step -1 {
?? i } }'

# ------------------------------------------------- array extents at declaration
# An extent is fixed when the declaration runs and never changes, so a size that can be
# settled statically is refused by the checker rather than by the interpreter. The two
# rules are separate: >= 1, and int - a real is refused outright, never truncated.
t "negative literal extent" "size must be 1 or more, got -2" 'fun <> = main() { real A[-2]
? 1 }'
t "zero extent" "size must be 1 or more, got 0" 'fun <> = main() { real A[0]
? 1 }'
t "folded negative extent" "size must be 1 or more, got -2" 'fun <> = main() { real A[3-5]
? 1 }'
t "negative in second dimension" "size must be 1 or more, got -3" 'fun <> = main() { real A[2][-3]
? 1 }'
t "real extent refused, not truncated" "size must be int, not real" 'fun <> = main() { real A[2.5]
? 1 }'
t "real variable extent refused" "size must be int, not real" 'fun <> = main() { real k : 2.
real m[k]
? 1 }'
# A static refusal must stop the run outright - the whole point is that nothing has
# happened yet when the diagnostic appears.
t "bad extent runs nothing" "size must be 1 or more" 'fun <> = main() {
? "ran first"
real A[-2] }'
# Only a size that genuinely depends on a variable reaches the interpreter. It reports the
# value it computed, which the checker's message can afford to omit but this one cannot -
# the number is nowhere in the source.
t "computed zero extent traps" "size must be 1 or more, got 0" 'fun <> = main() { k : 2
real m[k-2][4]
? 1 }'
t "computed negative extent traps" "size must be 1 or more, got -3" 'fun <> = main() { k : 2
real m[k-5]
? 1 }'
t "computed extent names the array" "'w': size must be" 'uses len
fun <> = f(v[]: real) { real w[len(v)-9]
? 1 }
fun <> = main() { real p[3] : {1.,2.,3.}
f(p) }'
t "literal extent still legal" "3" 'uses len
fun <> = main() { real A[3]
? len(A) }'
t "folded extent still legal" "6" 'uses len
fun <> = main() { real A[2*3]
? len(A) }'
t "len() extent still legal" "5" 'uses len
fun <> = f(v[]: real) { real A[len(v)]
? len(A) }
fun <> = main() { real v[5] : {1.,2.,3.,4.,5.}
f(v) }'
# len() is capacity for every array, a string included. It used to report a string'"'"'s
# content length, which gave it two domains: never 0 for a numeric array, but 0 for an
# empty string - so "real w[len(s)]" failed on a value that was not a mistake.
t "len of a string is capacity" "8" 'uses len
fun <> = main() { char s[8] : "abc"
? len(s) }'
t "len of an empty string" "8" 'uses len
fun <> = main() { char s[8] : ""
? len(s) }'
t "len() is never 0" "4" 'uses len
fun <> = main() { char s[4] : ""
real w[len(s)]
? len(w) }'

# --------------------------------------------------- array literals in assignment
# A brace literal is accepted wherever a declaration accepts one. An array still cannot
# be created from an arbitrary expression, because its extents could not be known - but
# a literal carries its own shape, so that one case is allowed to create it.
t "literal into a declared array" "1.000000  2.000000 3.000000  4.000000" 'fun <> = main() { real A[2][2]
A : {{1.,2.},{3.,4.}}
? A }'
t "1-D literal in assignment" "1.000000  2.000000  3.000000" 'fun <> = main() { real v[3]
v : {1.,2.,3.}
? v }'
t "int literal widens to real" "1.000000  2.000000" 'fun <> = main() { real A[1][2]
A : {{1,2}}
? A }'
t "the = form takes one too" "7.000000  8.000000" 'fun <> = main() { real v[2]
v = {7.,8.}
? v }'
t "a literal may create an array" "2 3" 'fun <> = main() { B : {{1.,2.,3.},{4.,5.,6.}}
? B.row B.col }'
t "a non-literal still may not" "Declare the array 'C' first" 'fun <> = main() { real A[2] : {1.,2.}
C : A
? C }'
# The copy goes into the storage the variable already has: extents are fixed at
# declaration, so assigning a smaller literal fills part of it rather than resizing it.
t "assignment fits, never resizes" "3 3" 'fun <> = main() { real A[3][3]
A : {{1.,2.},{3.,4.}}
? A.row A.col }'
# Which is what lets a by-reference parameter be filled from a literal - rebinding the
# storage would have resized the caller'"'"'s array out from under it.
t "literal into a reference param" "9.000000  9.000000 9.000000  9.000000" 'fun <> = fill(M[][]: real) {
M : {{9.,9.},{9.,9.}}
}
fun <> = main() { real A[2][2]
fill(A)
? A }'
t "scalar assignment unaffected" "6" 'fun <> = main() { x : 5
x : x + 1
? x }'

# ------------------------------------------------------- omitted entries in a literal
# A slot left empty is an omitted entry, not a syntax error: the commas alone fix the
# shape. It stands for the zero of the element type, which is the same thing a literal
# that stops short of the extents leaves behind - absence means zero either way.
t "elision writes zeros around it" "1.000000  0.000000  0.000000 0.000000  1.000000  0.000000 0.000000  0.000000  1.000000" 'fun <> = main() { M : {{1.0,,},{,1.0,},{,,1.0}}
? M }'
t "elided literal keeps its shape" "3 3" 'fun <> = main() { M : {{1.0,,},{,1.0,},{,,1.0}}
? M.row M.col }'
t "whole rows may be blank" "0.000000  0.000000  0.000000 0.000000  0.000000  0.000000 1.000000  1.000000  1.000000" 'fun <> = main() { K : {{,,},{,,},{1.,1.,1.}}
? K }'
t "an int literal elides to int 0" "1  0  0 0  1  0" 'fun <> = main() { N : {{1,,},{,1,}}
? N }'
# A blank carries no type, so a leading gap must not decide the literal is int.
t "a leading blank does not set the type" "0.000000  1.500000  0.000000" 'fun <> = main() { R : {,1.5,}
? R }'
t "elision fills a declared array" "3 3" 'fun <> = main() { real M[3][3] : {{1.,,},{,1.,},{,,1.}}
? M.row M.col }'
# Blank all the way down has a shape but no type to read - fine once the array is
# declared, since the declaration supplies the type, and an error when it is not.
t "all-blank cannot create" "An all-blank literal cannot create 'Z'" 'fun <> = main() { Z : {{,,},{,,},{,,}}
? Z }'
t "all-blank is fine when declared" "0.000000  0.000000 0.000000  0.000000" 'fun <> = main() { real Z[2][2] : {{,},{,}}
? Z }'
# The other half of "absence means zero": a literal that stops short no longer leaves
# the cells beyond it holding whatever was there before.
t "a short literal zeroes the rest" "9.000000  9.000000  0.000000 9.000000  9.000000  0.000000 0.000000  0.000000  0.000000" 'fun <> = main() { real M[3][3] : {{5.,5.,5.},{5.,5.,5.},{5.,5.,5.}}
M : {{9.,9.},{9.,9.}}
? M }'
# Clearing resets the contents, not the storage, so a by-reference fill still reaches
# the caller and still does not resize it.
t "elision through a reference param" "9.000000  0.000000  0.000000 0.000000  9.000000  0.000000" 'fun <> = fill(M[][]: real) {
M : {{9.,,},{,9.,}}
}
fun <> = main() { real A[2][3]
fill(A)
? A }'
# Row assignment is deliberately the exception: it replaces the row rather than filling
# it, which is what keeps dimensions measured rather than declared.
t "row assignment still replaces" "2" 'fun <> = main() { real A[2][5]
A[0] : {1.,2.}
? A[0].row }'
# Consequence of slots being counted by commas: a trailing comma is now a blank slot
# rather than a parse error. '{1.,2.,}' is three entries, the last one zero.
t "a trailing comma is a slot" "3" 'fun <> = main() { v : {1.,2.,}
? v.row }'
t "string assignment unaffected" "hi" 'fun <> = main() { char s[8] : "abcdef"
s : "hi"
? s }'

# ------------------------------------------------------------ printing an array as a grid
# "? A" lays a numeric array out in columns, so a program needs no display function of its
# own. Reals are fixed to 7 decimal places: a grid is read down its columns, and full
# Double precision ragged them and filled them with rounding noise.
t "2-D real prints as a grid" "1.000000  2.000000 3.000000  4.000000" 'fun <> = main() { real A[2][2] : {{1.,2.},{3.,4.}}
? A }'
t "1-D real stays on one line" "1.500000  2.250000" 'fun <> = main() { real v[2] : {1.5,2.25}
? v }'
t "int grid has no decimals" "1   2 40  50" 'fun <> = main() { int k[2][2] : {{1,2},{40,50}}
? k }'
t "columns are right-aligned" "1.000000  10.000000" 'fun <> = main() { real A[1][2] : {{1.,10.}}
? A }'
# A label keeps its own line, so the first row is not pushed out of column by it.
t "label then matrix breaks line" "A = 1.000000" 'fun <> = main() { real A[1][1] : {{1.}}
? "A =" A }'
# A string is not a grid - it still prints inline, exactly as before.
t "string still prints inline" "name: abc" 'fun <> = main() { char s[8] : "abc"
? "name:" s }'
# A scalar real keeps full precision; only a grid cell is rounded.
t "scalar real is 7 places" "0.3333333" 'fun <> = main() { x : 1./3.
? x }'

# ------------------------------------------- 6 declarations sit anywhere in a body
# The rule was "top of the function, never inside an if or a loop", and it is gone.
# It was refused in the parser, so it never reached the checker at all.
#
# What did NOT change is the lifetime: a declared local is still the whole call's, one
# name to one variable of one type, which is what lets a function be typed in one pass.
# So the name is VISIBLE only to the end of its block - the rule a name made by a first
# assignment has always followed - and two sibling blocks may not each declare it.
# Visibility ending with the block is what keeps the checker and the run in step: the
# interpreter's box goes with the block too.
t "declare inside an if" "8" 'fun <> = main() { int x : 5
if x > 0 {
int t : 3
? x + t } }'
t "declare inside a loop body" "2  4  6" 'fun <> = main() {
for i : 1 to 3 {
int t : i * 2
? t } }'
t "declare after a statement, C style" "1  3" 'fun <> = main() { int a : 1
? a
int b : 2
? a + b }'
t "an array may be declared in a block" "4" 'uses len
fun <> = main() { int n : 4
if n > 0 {
real v[n]
? len(v) } }'
# Read outside the block that declared it and it is not there - the same answer a name
# made by assignment in that block has always given.
t "read after the block is refused" "Undefined variable 't'" 'fun <> = main() { int x : 5
if x > 0 {
int t : 3 }
? t }'
# One name, one variable, for the whole call. The first block's scope is long gone by
# the time the second is read, so this is remembered separately from the scope stack.
t "sibling blocks may not both declare it" "Variable 't' already defined" 'fun <> = main() { int x : 5
if x > 0 {
int t : 1 }
if x > 1 {
int t : 2 }
? x }'
t "an inner declaration may not shadow an outer" "Variable 't' already defined" 'fun <> = main() { int t : 1
if t > 0 {
int t : 2 }
? t }'
t "a parameter still cannot be redeclared" "Variable 'a' already defined" 'fun <> = f(a: int) {
if a > 0 {
int a : 2 }
return }
fun <> = main() { f(1) }'
# The global warning reaches a nested declaration too, since it is the same check.
t "a nested declaration still warns on a global" "hides a global" 'int g : 1
fun <> = main() {
if g > 0 {
int g : 2
? g } }'

# ------------------------------------------------------------ statements / control
t "hello world" "Hello world!" 'fun <>=main() {
   ? "Hello world!"
}'
t "compound assign +:" "6" 'fun <> = main() { x : 5 x +: 1
? x }'
t "compound assign -:" "4" 'fun <> = main() { x : 5 x -: 1
? x }'
t "if / else if / else" "mid" 'fun <> = main() { x : 5 if x > 9 {
? "hi" } else if x > 2 {
? "mid" } else {
? "lo" } }'
t "while loop" "1 2 3" 'fun <> = main() { i : 1 while i < 4 {
?? i i +: 1 } }'
t "for loop" "1 2 3" 'fun <> = main() { for i:1 to 3 {
?? i } }'
t "for loop with step" "1 3 5" 'fun <> = main() { for i:1 to 5 step 2 {
?? i } }'
t "for loop counts down" "3 2 1" 'fun <> = main() { for i:3 to 1 step 0-1 {
?? i } }'
# A real anywhere in the bounds widens the counter, so the loop runs in reals.
t "real bound widens the counter" "1.0000000 2.0000000" 'fun <> = main() { for i:1 to 2.9 {
?? i } }'
t "zero step errors" "Step value cannot be zero" 'fun <> = main() { for i:1 to 5 step 0 {
?? i } }'
# 3.0: int division by zero is an error. 2.x produced inf, which then reached Int() and
# trapped uncatchably wherever a loop bound or an index was computed from it.
t "int division by zero" "Division by zero" 'fun <> = main() {
? 1/0 }'
t "int modulus by zero" "Division by zero" 'fun <> = main() {
? 1%0 }'
t "real division by zero is inf" "inf" 'fun <> = main() {
? 1./0. }'
# Int(Double) traps - uncatchably - on NaN/infinity/out-of-range, and a builtin result
# reaches these bounds directly. Each of these used to kill the process with no output.
t "NaN loop end" "Loop end out of range" 'uses sqrt
fun <> = main() { for i:1. to sqrt(0.-1.) {
?? i } }'
t "infinite loop end" "Loop end out of range" 'fun <> = main() { for i:1. to 1./0. {
?? i } }'
t "NaN loop start" "Loop start out of range" 'fun <> = main() { for i:0./0. to 5. {
?? i } }'
t "NaN loop step" "Loop step out of range" 'fun <> = main() { for i:1. to 5. step 0./0. {
?? i } }'
t "out-of-Int-range loop end" "Loop end out of range" 'uses pow
fun <> = main() { for i:1. to pow(10.,400.) {
?? i } }'
t "bad bound reports, not traps" "before  Error: line 3: Loop end" 'fun <> = main() {
? "before"
for i:1. to 0./0. {
?? i } }'
# A step pointing away from the end is a loop that runs zero times. It is a warning, not an
# error - an empty count is legitimate, which is why 'for j < v.col' over a vector is allowed
# to run none - so the program still runs and the rest of it still prints.
t "step away from the end warns" "Loop never runs: 'i' starts at 10 and step 1 moves away from 1" 'fun <> = main() { for i : 10 to 1 step 1 {
?? i }
? "done" }'
t "the warning does not stop the run" "done" 'fun <> = main() { for i : 10 to 1 step 1 {
?? i }
? "done" }'
t "a missing step is the same mistake" "Loop never runs: 'i' starts at 10 and step 1 moves away from 1" 'fun <> = main() { for i : 10 to 1 {
?? i }
? "done" }'
t "a negative step counting up warns" "Loop never runs: 'i' starts at 1 and step -1 moves away from 10" 'fun <> = main() { for i : 1 to 10 step -1 {
?? i }
? "done" }'
t "a folded bound is judged too" "Loop never runs: 'i' starts at 10 and step 1 moves away from 2" 'fun <> = main() { for i : 5*2 to 4-2 {
?? i }
? "done" }'
t "real bounds are judged too" "Loop never runs: 'x' starts at 2 and step -1 moves away from 3" 'fun <> = main() { for x : 2. to 3. step -1. {
?? x }
? "done" }'
# The loops that do run, and the ones whose direction cannot be read off the source, stay quiet.
t "counting down is not a warning" "3 2 1" 'fun <> = main() { for i : 3 to 1 step -1 {
?? i } }'
t "counting up is not a warning" "1 2 3" 'fun <> = main() { for i : 1 to 3 {
?? i } }'
t "one pass is not a warning" "5" 'fun <> = main() { for i : 5 to 5 {
?? i } }'
t "a computed bound is not judged" "none" 'fun <> = main() { real v[4]
for j < v.col {
?? "ran" }
? "none" }'
t "a variable bound is not judged" "none" 'fun <> = main() { n : 1
for i : 10 to n {
?? "ran" }
? "none" }'
# Zero has no direction; it is refused at run time with a message of its own.
t "a zero step is still the zero-step error" "Step value cannot be zero" 'fun <> = main() { for i : 10 to 1 step 0 {
?? i } }'

t "return out of if" "9" 'fun <int> = f(n: int) { if n > 0 { return 9 } return 1 }
fun <> = main() {
? f(5) }'
t "return out of while" "3" 'fun <int> = f() { i : 0 while i < 9 { i +: 1 if i = 3 { return i } } return 0 }
fun <> = main() {
? f() }'
t "quadratic program" "Solution 1.0000000 1.0000000" 'uses sqrt
fun <> = main() {
   a : 1.
   b : -2.
   c : 1.
   d : b^2.-4.*a*c
   if d < 0. {
? "No real roots" }
   else { d : sqrt(d) x1 : (0.-b-d)/(2.*a) x2 : (0.-b+d)/(2.*a)
? "Solution" x1 x2 }
}'

# ------------------------------------------------- 5.3 char never joins arithmetic
# Mixing a char with a number was always refused. char-with-char was not: widest() of
# two equal types is that type, so nothing was left to disagree about and 's[0]+s[1]'
# came back a real. Comparison and ordering stay legal - that is what char is for.
t "char plus char refused" "'+' does not apply to char" 'fun <> = main() { char s[8] : "ab"
? s[0] + s[1] }'
t "char times char refused" "'*' does not apply to char" 'fun <> = main() { char s[8] : "ab"
? s[0] * s[1] }'
t "char minus char refused" "'-' does not apply to char" 'fun <> = main() { char s[8] : "ab"
? s[0] - s[1] }'
t "char plus int still refused" "Cannot use int where char is required" 'fun <> = main() { char s[8] : "ab"
? s[0] + 1 }'
t "char orders against char" "1" 'fun <> = main() { char s[8] : "ab"
? s[0] < s[1] }'
t "char compares equal" "1" 'fun <> = main() { char s[8] : "ab"
? s[0] = s[0] }'
t "int(c) is how a char reaches arithmetic" "98" 'fun <> = main() { char s[8] : "ab"
? int(s[0]) + 1 }'
t "joining two strings is untouched" "abcd" 'fun <> = main() { char a[8] : "ab"
char b[8] : "cd"
? a + b }'

# ------------------------------------------------- 2.4 a string literal must be closed
# The closing quote used to be optional and [^"]* matched newlines, so one stray quote
# ate the rest of the file and reported a parse error far below, on a line that was fine.
t "unclosed string" "Unclosed string" 'fun <> = main() {
? "hello }'
t "unclosed string reports its own line" "line 2" 'fun <> = main() {
? "hello
? "world" }'
t "a closed string is unaffected" "a, b: c (d)" 'fun <> = main() {
? "a, b: c (d)" }'
t "an empty string is still a string" "ok" 'fun <> = main() {
? "" "ok" }'

# ------------------------------------------------- 5.5 the output list is required
# 'fun = main()' used to parse and run - a second spelling of the header the grammar
# never described.
t "output list is required" "Unexpected '='" 'fun = main() {
? 1 }'
t "an empty output list is still written" "1" 'fun <> = main() {
? 1 }'

# --------------------------------------------------------- break / continue
# Loop control binds to the innermost loop and nothing else. 'if' raises the block
# depth but not the loop depth, which is what makes a break inside an if legal and a
# break inside a bare if illegal - the two cases either side of that line are both
# here because one counter serving both jobs would pass one and fail the other.
t "break leaves a while" "0 1 2 3 done" 'fun <> = main() {
  i : 0
  while 1 {
    if i > 3 { break }
    ?? i
    i : i + 1
  }
? "done" }'
t "break leaves a for" "0 1 2 done" 'fun <> = main() {
  for i < 10 {
    if i = 3 { break }
    ?? i
  }
? "done" }'
t "continue skips the rest of the pass" "1 3 5" 'fun <> = main() {
  for i : 0 to 6 {
    if i % 2 = 0 { continue }
    ?? i
  }
? "" }'
t "continue still advances the counter" "0 1 2 3 4" 'fun <> = main() {
  for i < 5 {
    ?? i
    continue
  }
? "" }'
t "break leaves only the innermost loop" "0 1 2" 'fun <> = main() {
  for i < 3 {
    for j < 3 {
      if j = 1 { break }
      ?? i
    }
  }
? "" }'
t "break in a while inside a for" "a a a" 'fun <> = main() {
  for i < 3 {
    while 1 {
      ?? "a"
      break
    }
  }
? "" }'
t "break outside a loop"  "'break' outside a loop" 'fun <> = main() {
  break
}'
t "continue outside a loop"  "'continue' outside a loop" 'fun <> = main() {
  continue
}'
t "break inside an if but outside a loop"  "'break' outside a loop" 'fun <> = main() {
  if 1 { break }
}'
t "break at global scope" "must be inside a function" 'break
fun <> = main() {
? 1 }'
t "loop depth resets between functions"  "'break' outside a loop" 'fun <> = g() {
  for i < 2 {
    ? i
  }
}
fun <> = main() {
  break
}'
t "break is a keyword, not a name"  "Unexpected ':'" 'fun <> = main() {
  for i < 2 {
    break : 5
  }
}'
t "BREAK folds case like every keyword"  "'break' outside a loop" 'fun <> = main() {
  BREAK
}'
# 'while 1 { ... return }' cannot finish without returning, and used to be reported
# as if it could. A break in that body puts the escape back, so the error returns.
t "while 1 with a return satisfies the output" "3" 'fun <int> = f() {
  while 1 { return 3 }
}
fun <> = main() {
? f() }'
t "while 1 with a break still needs a return" "can finish without a return" 'fun <int> = f() {
  while 1 {
    if 1 { break }
    return 3
  }
}
fun <> = main() {
? f() }'
# The break belongs to the inner for, so it does not let the outer while finish -
# the return below it still runs on every path, and the function is accepted.
t "a nested loop owns its own break" "3" 'fun <int> = f() {
  while 1 {
    for i < 2 { break }
    return 3
  }
}
fun <> = main() {
? f() }'

# ----------------------------------------------------- parser regressions guarded
# Each of these failed at some point during the parser refactor; they are the
# reason this file exists.
t "REG return before }" "1" 'fun <int> = f() { return 1 }
fun <> = main() {
? f() }'
t "REG bare call statement" "9" 'fun <int> = f() { return 1 }
fun <> = main() { f()
? 9 }'
t "REG = fallback assignment" "5" 'fun <> = main() { x = 5
? x }'
t "REG print starting with -" "4" 'fun <> = main() {
? -2^2 }'
t "REG print starting with (" "3" 'fun <> = main() {
? (1+2) }'
t "REG parseError reaches caller" "Error: line 1: Unexpected '*'" 'fun <> = main() { x : * 5 }'

# ------------------------------------------------------------------- precedence
t "power is right-associative" "512" 'fun <> = main() {
? 2^3^2 }'
t "unary minus binds over ^" "4" 'fun <> = main() { x : -2^2
? x }'
t "inline print ??" "hello world" 'fun <> = main() {
?? "hello"
?? "world" }'
t "mixed string and numbers" "Solution 3 5" 'fun <> = main() { x:3 y:5
? "Solution" x y }'

# ------------------------------------ 5.10 print commands own their line ('?', '??')
# The rule needs Token.line: without it "? x ?? x" and the same two commands on two
# lines are the identical token stream, so neither could be rejected without the other.
t "? must start its line" "must start its line" 'fun <> = main() { x : 1 ? x }'
t "?? must start its line" "must start its line" 'fun <> = main() { x : 1 ?? x }'
t "two commands one line" "'??' must start its line" 'fun <> = main() { x : 1
? x ?? x }'
t "two ? one line" "'?' must start its line" 'fun <> = main() { x : 1
? x ? x }'

# A print command owns its line at *both* ends: it must open the line, and its items stop
# where the line does. Before the second half existed, the statement after a print was read
# as more items whenever it began with something startsTerm accepts - a parse error for an
# indexed assignment, and silently wrong output for a bare call, which ran early and had its
# result printed. No token pattern separates those from real items: "? l f(x)" and a bare
# "f(x)" on the next line are the same tokens in the same order, so only the line tells.
t "indexed assign after print" "1.0000000  9.0000000" 'fun <> = main() { real v[2] : {1.,2.}
? v[0]
v[1] : 9.
? v[1] }'
t "bare call after print keeps order" "label  inside" 'fun <> = hi() {
? "inside" }
fun <> = main() {
? "label"
hi() }'
t "call still works as an item" "root 4.0" 'uses sqrt
fun <> = main() {
? "root" sqrt(16.) }'
t "items still run to end of line" "x 3 sq 9" 'fun <> = main() { x : 3
? "x" x "sq" x*x }'
t "indexed item on the same line" "v0 7.0000000 v1 8.0000000" 'fun <> = main() { real v[2] : {7.,8.}
? "v0" v[0] "v1" v[1] }'
t "attribute item on the same line" "rows 2 cols 2" 'fun <> = main() { real A[2][2]
? "rows" A.row "cols" A.col }'
t "command after item list" "must start its line" 'fun <> = main() { x : 1
? "hello" ?? x }'
# Indentation is invisible here - the lexer drops it before the parser sees a line at all.
t "indented command is fine" "5" 'fun <> = main() {
     x : 5
        ? x
}'
# 3.0: only declarations and definitions live outside a function, so a print can no
# longer be the first token of the file. 2.x allowed statements at top level.
t "no statements in global space" "must be inside a function" '? 9
fun <> = main() {
? 7 }'
t "global declaration is allowed" "4" 'int g : 4
fun <> = main() {
? g }'
# A global is visible below the line that declares it and nowhere above it. Functions are
# not ordered this way - main() may call something written after it - but a global cannot
# be, because the interpreter creates the globals in file order: when the checker treated
# them as a set and the interpreter as a sequence, a program the checker passed could still
# die at run time on a name it had accepted.
t "a global is visible below its declaration" "3" 'int c : 0
fun <> = hello() { for i < 3 { c : c + i }
? c }
fun <> = main() { hello() }'
t "a global is not visible above it" "'c' is a global declared later, on line 3" 'fun <> = hello() { for i < 3 { c : c + i }
? c }
int c : 0
fun <> = main() { hello() }'
t "the message names the declaration line" "on line 4" 'fun <> = f() {
? g }
fun <> = main() { f() }
int g : 1'
# The run-time failure this closes: the checker passed this program, then the interpreter
# built the globals in order and f() reached one that did not exist yet.
t "a global initializer cannot reach a later global" "'b' is a global declared later, on line 3" 'int a : f()
fun <int> = f() { return b }
int b : 5
fun <> = main() {
? a b }'
t "an initializer may reach an earlier global" "5 5" 'int b : 5
int a : b
fun <> = main() {
? a b }'
# A name that is nowhere in the file still reports as undefined - the new message is only
# for one that is there, spelled correctly, further down.
t "a name that is nowhere is still undefined" "Undefined variable 'zz'" 'fun <> = main() {
? zz }'
# "!" lost its print role to "??" and is now only the first half of "!=".
t "bare ! is not a command" "'!' is not a command" 'fun <> = main() { x : 1
! x }'
t "! keeps != intact" "1" 'fun <> = main() { x : 1
? x != 2 }'
t "!= is not two commands" "0" 'fun <> = main() {
? 2 != 2 }'

# ------------------------------------------------------- 10 error line reporting
# Every diagnostic names its line, whatever stage produced it. Blank lines and comments count, so
# these double as a check that the lexer's newline counting isn't skipping anything.
t "lex error names line" "Error: line 3:" 'fun <> = main() {
 x : 1
 хn : 5
}'
t "lex line counts blanks" "Error: line 5:" 'fun <> = main() {

 // a comment

 y : 1.2.3
}'
t "parse error names line" "Error: line 3:" 'fun <> = main() {
 x : 1
 x : 2 ? x
}'
t "end of input names last line" "Error: line 3: Missing" 'fun <> = main() {
 x : 1
 ? x'
t "check error names line" "Error: line 4: Undefined variable" 'fun <> = main() {
 x : 1
 y : 2
 ? zz
}'
# The line reported is the statement the name appears in, so a name used inside a
# callee names the callee's line rather than the call site's.
t "error line is the callee's" "Error: line 2: Undefined variable 'qq'" 'fun <int> = f() {
 return qq
}
fun <> = main() {
 ? f()
}'
# 3.0: missing main() and redefinition are both caught by the checker, before anything
# runs. 2.x discovered them at run time, so earlier output had already appeared.
t "no main is reported" "No main() function defined" 'fun <int> = g() { return 1 }'
t "redefinition names both lines" "Function 'g' already defined (line 1)" 'fun <> = g() { return 1 }
fun <> = g() { return 2 }
fun <> = main() {
? g() }'

# ------------------------------------------------- end of input at every position
# Truncated source must produce a diagnostic - never a hang, crash, or silent pass.
while IFS= read -r snippet; do
    t "EOF: $snippet" "Error: line" "$snippet"
done <<'SNIPPETS'
fun
fun <
fun <> = main()
fun <> = main() {
fun <> = main() { return
fun <> = main() { ?
fun <> = main() { x :
fun <> = main() { ? (1
fun <> = main() { if
fun <> = main() { for i:1 to
fun <> = main() { <a,b> :
fun <> = main() { for i <
fun <> = main() { for i < n step
fun <> = main() { x : y.
fun <> = main() { x : y.dim(
fun <int> = f(a
SNIPPETS

# ---------------------------------------------------------------------- examples
# Examples/*.shm are the programs printed on the scan sheets, so they are the closest
# thing here to an end-to-end test: if one stops running, what gets photographed off
# paper stops working. They also cover ground the unit cases do not - array arguments
# by reference, nested functions, and grid output at real sizes.
#
# Every program in Examples/ is run, and every '// expected:' line it carries is
# asserted against its output. The expectations live in the program rather than in a
# list here, so a file states its own contract and dropping one into the directory
# tests it - there is no second copy to keep in step, and no way to add an example
# the suite quietly ignores. A file with no '// expected:' line is itself a failure,
# which is what stops the directory filling with programs nothing checks.
for path in "$ROOT"/Examples/*.shm; do
    name=$(basename "$path")
    out=$(perl -e 'alarm 10; exec @ARGV' "$BUILD/shalimar" "$path" 2>&1 | tr '\n' ' ')
    wanted=0
    while IFS= read -r want; do
        wanted=$((wanted + 1))
        if [[ "$out" == *"$want"* ]]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            failures+=("example $name"$'\n'"      want: $want"$'\n'"      got:  $out")
        fi
    done < <(sed -n 's|^// expected: ||p' "$path")
    if (( wanted == 0 )); then
        fail=$((fail + 1))
        failures+=("example $name"$'\n'"      carries no '// expected:' line")
    fi
done

# ------------------------------------------------------- scan layout (OCR line rebuild)
# A second binary: ScanLayout.swift is part of the app but has no UIKit or Vision
# dependency precisely so its line-rebuilding can be tested here. Its failures are folded
# into this suite's counts so the hook and CI see one number.
scan_out=$(swiftc -O "$SRC/ScanLayout.swift" "$ROOT/Tests/scanlayout/main.swift" \
                  -o "$BUILD/scanlayout" 2>&1) && "$BUILD/scanlayout"
scan_status=$?
if (( scan_status != 0 )); then
    fail=$((fail + 1))
    failures+=("scan layout tests"$'\n'"      see output above"$'\n'"      $scan_out")
fi

# ------------------------------------------------------------ indent (editor layout)
# A third binary, same arrangement and for the same reason: Indent.swift decides where a
# brace and the line after it belong, and is kept clear of UIKit so it can be run here.
# It was extracted from ComputeViewController after a bug that only typing into a
# simulator could find - a brace pushed off `if n < 2 { return 1 }` landing under the
# body instead of under the `if`. Nothing in the editor's layout is reachable from a
# test unless it lives in a file like this one.
indent_out=$(swiftc -O "$SRC/Indent.swift" "$ROOT/Tests/indent/main.swift" \
                    -o "$BUILD/indent" 2>&1) && "$BUILD/indent"
indent_status=$?
if (( indent_status != 0 )); then
    fail=$((fail + 1))
    failures+=("indent tests"$'\n'"      see output above"$'\n'"      $indent_out")
fi

# ------------------------------------------------------------------------ report
echo
echo "PASS: $pass   FAIL: $fail"
if (( fail > 0 )); then
    echo
    echo "FAILURES:"
    for f in "${failures[@]}"; do
        echo "  - $f"
    done
fi
exit $(( fail > 0 ? 1 : 0 ))
