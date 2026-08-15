# Slang Compiler SDK

This directory contains the minimal Windows x86-64 Slang compiler SDK used by
Vanguard's offline shader tools. It is version 2026.14.1 and originates from
the official binary release package.

Only offline tools may include or link this SDK. Runtime rendering, RHI,
resource loading, and Shipping applications consume cooked `.vshader` data and
must not depend on Slang.

Vendored components:

- Public compiler API headers.
- `slang-compiler.lib` and `slang-compiler.dll`.
- Compiler modules required by the DXIL and SPIR-V tool paths.
- Upstream license and third-party license texts.

The upstream graphics layer, command-line programs, LLVM backend, and example
content are intentionally excluded.
