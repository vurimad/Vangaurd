# Vanguard Containers

Vanguard Containers is a complete working adaptation of RED Containers. The
entire RED image is compiled behind `redContainersCompat`; normal engine code
uses `vanguard/containers/containers.hpp` and the
`vanguard::containers` namespace.

## Initialization

Memory must be initialized first:

```cpp
vanguard::memory::Initialize();
vanguard::containers::Initialize();
```

Initialization is an explicitly serialized composition-root operation.
Calling Containers before Memory returns `false`. Repeated successful
initialization is idempotent.

## Pool ownership

Every owning container that accepts a pool preserves RED's pool-reference
contract:

```cpp
vanguard::containers::DynamicArray<RenderItem> items{
    vanguard::memory::pools::Rendering()};
```

The selected pool is retained across allocation, growth, reallocation, copy
into an explicit pool, move, and destruction. Vanguard does not enable RED's
default-container-pool compatibility switch; new owning-container call sites
must choose their pool explicitly. `String` retains RED's dedicated string
pool behavior and also supports an explicit pool.

## Complete image

The compiled and exposed families include:

- dynamic, static, fixed, sorted, packed, wrapped, and circular arrays;
- array spans, iterators, buffers, blobs, and views;
- hash and ordered maps and sets;
- fixed and dynamic bitsets;
- queues, priority queues, lock-free queues, heaps, and intrusive lists;
- block pools, ID allocators, index allocators, and LRU ID pools;
- ANSI, UTF-8, and UTF-16 strings, views, builders, tokenizers, and utilities;
- RED container algorithms, hash policies, construction policies, debugger
  visualizers, and formatters.

Both RED-compatible names such as `DynArray` and the preferred Vanguard
`DynamicArray` spelling are available. They refer to the same implementation
and ABI.

## Adaptation boundary

`include/vanguard/containers/containers.hpp` is the sole public bridge to the
working RED image. Callers do not name `red::` types. Compiler-compatibility
patches inside the imported image are limited to warning-clean correctness
changes such as explicit sentinel conversion, non-shadowing locals, and
preserving values used only by compiled-out assertions.

## Verification

`containersTests` validates pool routing, allocation baselines, growth,
copy/move behavior, element lifetimes, spans, arrays, maps, sets, queues,
packed storage, bitsets, lock-free queues, blobs, strings, and views in Debug,
Development, Profile, and Shipping.
