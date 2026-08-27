# Vendored converter

`Converter/src` is copied from **Converter-C2S** at `107bfd9`, minus
`main.cpp` — that file is the command-line wrapper, and this app supplies its
own entry point.

**Copied, not referenced.** The app has to build without a sibling checkout
present, and it ships to a phone where no such directory exists. The cost is
the usual one: this can drift. Refresh it the way Converter-C2S refreshes its
own vendored Shalimar front end — re-copy, rebuild, run the suite.

    rsync -a --exclude 'main.cpp' ../Converter-C2S/src/ Converter/src/

**What is in here does not include a compiler.** The converter carries its own
C front end (`c/CLexer`, `c/CParser`, `c/CAst`, `c/CMacro`, `c/CPreScan`)
and a vendored Shalimar front end (`s/vendor/`). It links neither cc1 nor shc,
so neither is in this app.

**It never leaves its process** — no `system`, `popen`, `fork` or `exec`
anywhere in these sources. That is what makes it legal on iOS: nothing is
generated and executed on the device.
