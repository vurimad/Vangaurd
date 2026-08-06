# Vanguard World Format (`.vworld`)

`.vworld` is the immutable global index for a cooked world. It deliberately contains no component payloads, Flecs entity IDs, renderer objects or source-asset paths. Those belong to `.vcell`, `.vprefab`, the renderer and the editor asset database respectively.

The document uses Vanguard's deterministic section envelope, CRC64 storage validation and SHA-256 logical-content fingerprint. Version 1.0 contains one `WRLD` section with four canonical tables: cells sorted by stable cell ID, distant proxies sorted by stable proxy ID, replacement children sorted by `(proxy ID, kind, child ID)`, and typed soft dependencies sorted by resource key. Input ordering therefore cannot change cooked bytes.

## Cell hierarchy

Cell level zero is the finest spatial level. Increasing values represent progressively coarser containment. A non-root cell references a parent with a strictly larger level, and the parent bounds must contain the child bounds. Activation distance controls entry; retention distance must be equal or larger and supplies unload hysteresis. Double-precision bounds, origins and query reference points keep the global index stable in large worlds, while `.vcell` continues to use compact single-precision local placement data.

## Distant representations

A distant proxy is a render-only `.vmesh` used while detailed cells or finer proxies are unavailable. It carries three query positions: the final streaming reference point, the pre-boost reference point used for near auto-hide, and a secondary reference point used to constrain large prefab visibility. Its basic distance contract is:

`nearHideDistance <= streamingDistance`

The secondary-reference distance is evaluated independently as a narrow-phase proxy query. Proxies can form an acyclic hierarchy; a parent must contain its child and must be visible at least as far away. A proxy's replacement-child range identifies cells or direct child proxies. At least one child must be marked `RequiredForReplacement` unless the proxy is explicitly `ProxyOnly`.

The runtime may hide a proxy near the observer only after all required children reach render-ready state. File completion alone is insufficient because geometry, materials and GPU residency may still be pending. This readiness rule prevents transient holes during slow or highly contended streaming.

Streaming priorities use spaced numeric tiers and sort in descending order: low and normal content occupy 0-1, specialized static/prefab/building/path tiers occupy 20-60, and critical content occupies 254-255. This ordering is part of the cooked contract.

## VPAK relationship

The `.vworld` resource and every referenced `.vcell`/proxy `.vmesh` can live in one or several VPAKs. References are stable typed resource IDs, not byte offsets into a particular package. The manifest dependency table uses soft edges so package planning and prefetch can see the relationship without recursively loading every world resource when the manifest opens.
