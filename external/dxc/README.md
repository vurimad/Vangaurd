# DirectX Shader Compiler Runtime

This directory contains the Windows x86-64 runtime subset of the stable DXC
1.9.2607 release. Slang uses it as the downstream compiler and validator when
Vanguard's offline shader tools generate DXIL.

DXC is deployed only beside offline tool and compiler-test executables. Runtime
rendering and Shipping applications consume cooked DXIL from `.vshader` files
and do not load DXC.
