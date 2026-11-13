add_compile_options(
    -std=c99                # Target the C99 standard (without gcc extensions)
    -pedantic               # Issue all the warnings demanded by strict ISO C
    -Wall                   # Warn about most questionable operations
    -Wstrict-prototypes     # Warn about missing prototypes
    -Wwrite-strings         # Warn if not treating string literals as "const"
    -Wpointer-arith         # Warn about suspicious pointer operations
    -Werror                 # Treat warnings as errors (code should be clean)
)
add_definitions(
    -D_XOPEN_SOURCE=600
)
