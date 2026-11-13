# Host tools for the cFS build: only the table tool, which cFE's mission
# build requires (CFS_TABLETOOL_SCRIPT_DIR). Mru's app uses no tables.
project(CFETOOLS C)
add_subdirectory(elf2cfetbl)
