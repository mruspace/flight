// ======================================================================
// \title  RustSupport.cpp
// \brief  The one symbol the quorum core's static library needs on 32-bit ARM
//
// On ARM, the unwind tables of Rust's compiler builtins name the Rust
// personality routine. The core is built with panic=abort, so nothing ever
// unwinds and the routine is never called; the linker only needs the name.
// ======================================================================
#if defined(__arm__)
extern "C" void rust_eh_personality() {}
#endif
