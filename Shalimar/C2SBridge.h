#ifndef C2S_BRIDGE_H
#define C2S_BRIDGE_H
/* Plain C, so Swift can see it. Nothing C++ leaks through this header. */
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   ok;            /* 1 when the whole program converted            */
    int   beyondCount;   /* constructs with no Shalimar form              */
    char *output;        /* the Shalimar source - free with c2s_free      */
    char *report;        /* one diagnostic per line - free with c2s_free  */

    /* Which line of the C each line of `output` came from, and how many
       entries there are - one per line written, 0 where no construct owns it.
       This is what turns everything said AFTER the conversion - by the lexer,
       the parser, the checker, the interpreter - back into a line of the file
       the author is actually looking at. Free with c2s_free. */
    int  *lines;
    int   lineCount;
} C2SResult;

C2SResult c2s_c_to_shalimar(const char *source, const char *name);

/* The other direction. Same result, with one field empty: `lines` maps output
   lines back to input lines so that a complaint about a file nobody can see
   can be aimed at the file on screen, and this direction has no such problem -
   the Shalimar is what the author is looking at, and the C is what they are
   being handed. Its diagnostics already carry the line they mean. */
C2SResult c2s_shalimar_to_c(const char *source, const char *name);

void      c2s_free(C2SResult *r);

#ifdef __cplusplus
}
#endif
#endif
