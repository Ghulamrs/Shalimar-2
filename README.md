# Shalimar 2.0

An iOS editor for **Shalimar** and for **C**, built on the interpreter this
project already has.

**Seeded from `../Shalimar` at `ebe4f17` on 2026-08-28**, with fresh git
history. The two are separate projects from that commit onward and are not
expected to converge; the original stays as it is.

## What it is, and what it is not

The Shalimar app runs Shalimar. This one is meant to run **C as well**, and it
does that without a compiler on the phone:

    C source  ->  c2s  ->  Shalimar source  ->  the interpreter already here

`c2s` is `../Converter-C2S`, a source-to-source converter in ISO C++14. It
never leaves its own process - no `system`, no `popen`, no `fork`, no `exec` -
so it links into the app as a static library and is called through a small
Objective-C++ bridge. Measured 2026-08-27: 21 translation units compile for
`arm64-apple-ios` with `-Wall -Wextra -Werror` and no changes, and contribute
about **362 KB** to the binary at `-Os`.

**Nothing is generated and run on the device.** That is what makes this
possible at all: iOS will not assemble, link, or load code an app produces, and
this design never asks it to. The compiling happens on a Mac, in Xcode, at
build time, like any app that links a C++ library.

## What C converts, and what does not

Measured against Compiler-C's own 425-case corpus, which is a *compiler* test
suite and therefore adversarial - dense with exactly what Shalimar lacks:

| | |
| --- | --- |
| convert in substance | 214 of 425 |
| of those that ran end to end, answered identically to the C | 152 of 167 |

What has no Shalimar form is a coherent set rather than an arbitrary one:
**struct and union members, pointers, `sizeof`, `long long`**, and `?:`/`++`
*inside* expressions. What does convert includes several things worth knowing
are covered: `switch` with fallthrough and shared labels, `do`-`while`,
compound assignment, block-scoped declarations, and `i++` as a statement.

`c2s` marks every construct it cannot express, in place:

    // #BEYOND SHALIMAR: main returning a status - Shalimar has none

For a teaching editor that is a feature rather than a failure - the student is
told which construct has no home and where, instead of being handed a program
that quietly will not run.

## Provenance

Seeded from `Shalimar` at `ebe4f17`. The language is Shalimar 3.0 as
`SHALIMAR_LANGUAGE.md` describes it; **`../Compiler-S` leads the language and
this follows it**, which is the direction settled on 2026-08-27.
