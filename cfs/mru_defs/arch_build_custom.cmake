add_compile_options(
    -std=c99    # Target the C99 standard (without gcc extensions)
    -pedantic   # Issue all the warnings demanded by strict ISO C
    -Wall       # Warn about most questionable operations
    -Werror     # Treat warnings as errors (code should be clean)
)
