# Vanguard Streaming

The streaming module is Vanguard's runtime resource-storage layer. It connects
the resource dependency pipeline to physical loose files and immutable VPAK
mounts without making the resources module depend on a concrete container
format.

## Contract

- One decoder is registered for each `ResourceTypeId`.
- Loose files and VPAK resources share the same canonical `ResourcePath`.
- The source with the highest numeric priority wins. A later source wins when
  priorities are equal.
- VPAK readers and their physical files remain valid and unchanged while
  mounted.
- Source registration and mount mutation are thread-safe. Mutation is refused
  while an affected source is participating in an active load.
- Reads are submitted to Vanguard's RED-derived asynchronous I/O service.
  Jobs workers do not block on file reads.
- VPAK segments are read directly into their final logical range when
  uncompressed. LZ4 segments use bounded compressed staging and are decoded by
  a Jobs construction stage.
- Every stored segment CRC is checked before copy or decompression. The final
  resource CRC is checked before its type decoder runs.
- All staging allocations use `memory::PoolId::Streaming` and are charged
  against one explicit global staging budget.
- Cancellation requests the underlying I/O contexts. Completion remains
  explicit: the I/O callback closes the asynchronous preparation only after
  every submitted read has returned.

This layer owns bytes only until the decoder callback returns. A decoder must
copy, transform, or otherwise take ownership of any data the resulting
`ResourceObject` needs.

Reflected resources register a `SchemaDecoderDescriptor`. Their VOBJ payload is
deserialized only after required dependency fan-in. Before publication, the
decoder checks the VPAK or loose manifest against every non-soft reflected
reference and binds loaded handles into the resource.

## Runtime use

```cpp
resources::ResourceRegistry registry;
resources::ResourcePipeline pipeline;
streaming::ResourceStreamer streamer;

registry.Initialize();
pipeline.Initialize(registry);
streamer.Initialize(pipeline, {
    .stagingBudgetBytes = 512ull * 1024ull * 1024ull
});

streamer.RegisterDecoder({
    meshType,
    "mesh",
    &DecodeMesh,
    &DestroyMesh,
    meshDecoderState
});

streamer.MountPackage(basePackage, basePackagePath, 0);
streamer.MountPackage(patchPackage, patchPackagePath, 100);

resources::PipelineRequest request =
    streamer.Request(meshReference, resources::LoadPriority::High);
request.Wait();
resources::ResourceHandle mesh = request.Acquire();
```

Runtime builds normally mount a complete DATA image through `PackageSetMount` instead of opening VPAKs individually:

```cpp
streaming::PackageSetMount packages;
streaming::PackageSetMountConfig packageConfig;
packageConfig.expectedGameId = gameId;
packageConfig.expectedTargetPlatformId = targetPlatformId;

if (packages.Mount(streamer, gameDirectory, packageConfig) != streaming::PackageSetMountResult::Success)
{
    return false;
}

resources::ResourceReference startupWorld = packages.StartupWorld();
```

The mount owner derives only canonical `DATA000.vpak` through `DATA999.vpak` names. It opens DATA000 first, validates the game/build/target
contract, then opens the exact catalog entries without scanning the directory. Missing optional entries may be skipped; missing required
entries fail the transaction. Exact file size, package/build identity, index CRC, and package structure are always checked before mount.
Whole-file SHA-256 verification is available through `CatalogVerification::WholeFileDigest` for installation validation and paranoid
modes, but is deliberately not the ordinary launch default because reading every byte of a large installed image would inflate startup.

All selected readers become visible through one `MountPackages` transaction. A failure leaves the streamer unchanged. DATA packages with
equal priority resolve in catalog order, while higher numeric priority wins regardless of number. Any package that would shadow an
earlier package must carry the explicit `Override` catalog flag. `Unmount` performs the inverse batch transaction and returns `Busy` if
one of its readers still participates in an active load. Destruction without explicit unmount is an API error.

Loose resources are registered with a typed logical reference, an absolute
physical path, explicit typed dependencies, an optional expected content CRC,
and a source priority. This provides editor and uncooked-data operation without
changing the runtime dependency or decoder path.

## Lifetime and shutdown

Composition roots initialize in this order:

```text
memory -> diagnostics -> containers -> io -> filesystem
       -> resource registry -> resource pipeline -> resource streamer
```

Shutdown reverses the owned layers:

1. Release every `PipelineRequest` and `ResourceHandle`.
2. Wait until pipeline and streamer active counts reach zero.
3. Explicitly unmount the `PackageSetMount`, then remove any individual package or loose registrations.
4. Unregister decoders.
5. Shut down the streamer, pipeline, registry, Jobs, filesystem, I/O,
   diagnostics, and finally memory at the application level.

`ResourceStreamer::Shutdown` refuses to proceed while reads, loads, or staging
allocations remain active. `Stats` exposes source counts, live work, staging
usage and peak, bytes read, integrity failures, cancellations, and budget
rejections for the editor and diagnostics layer.

## Deliberate exclusions

This module does not own concrete asset schemas, hot reload, source import,
cooking, encryption, remote delivery, world-cell policy, or GPU upload.
Concrete layouts remain in their owning modules. Material object layouts are
resolved after shader dependency fan-in, allowing shader interface metadata to
generate the reflected material schema instead of fixing one engine-wide
`MaterialData` structure.
