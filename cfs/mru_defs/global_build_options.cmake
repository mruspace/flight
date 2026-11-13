# Resource IDs in the simple mode, as NASA's simple configuration
set(MISSION_RESOURCEID_MODE "SIMPLE")

# As NASA's simple configuration: optimized builds of cFE trip these
# warnings on safe uses of strncpy and snprintf
add_compile_options_if_available(C
  -Wstrict-prototypes      # Warn about missing prototypes
  -Wwrite-strings          # Warn if not treating string literals as "const"
  -Wpointer-arith          # Warn about suspicious pointer operations
  -Wno-format-overflow     # Inhibit printf-style format overflow warnings
  -Wno-format-truncation   # Inhibit printf-style format truncation warnings
  -Wno-stringop-overflow   # Inhibit string operation overflow warnings
  -Wno-stringop-truncation # Inhibit string operation truncation warnings
)
