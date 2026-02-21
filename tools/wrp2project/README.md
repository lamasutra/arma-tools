# wrp2project Notes

## TV4L/TB Compatibility Findings

This document summarizes the object-layer (`*.tv4l`) compatibility issues found while comparing:

- TB-native project output (`cup-cain-e`)
- `wrp2project` output (`cup-cain-*` test iterations)

All conclusions below are based on:

- behavior in Terrain Builder,
- binary comparison against TB-generated files and observed behavior in Terrain Builder,
- black-box compatibility testing and file-diff analysis.

## Confirmed Critical Rules

### 1) Template hash encoding must differ by file type

- Hash algorithm: `SDBM`.
- In `TemplateLibs/*.tml`, `<Hash>` must be written as **signed int32 decimal text**.
- In `*.tv4l` tree records, template hash is stored as raw **uint32 bits**.
- Never clamp high-bit hashes (for example to `INT_MAX`), otherwise hash collisions occur and templates resolve incorrectly.

### 2) Object-layer root bbox must be georeference-based

For object layers, TB uses a wide fixed envelope around projected center:

- `x = center_easting +/- 450000`
- `y = [0, 1340000]`

Using `offset_x + world_size/2` as center produced wrong root bbox and caused the classic symptom:

- objects only appear after **Rebuild object tree** in TB.

Fix: compute center from georef (UTM from lon/lat + zone), matching TB behavior.

### 3) Root/inner node object count field behavior

- Root/inner node `obj_count` in TB-native trees is `0`.
- Non-zero values in this field can lead to inconsistent load behavior.

`wrp2project` now writes `0` for inner-node count field.

### 4) Quadtree child ordering matters

Verified TB-compatible order for this serializer:

- `0=SE`, `1=NE`, `2=SW`, `3=NW`

Mismatched order changes tree distribution and can break placement/selection behavior.

### 5) Object IDs in TV4L stream start at 10000

Observed TB behavior and compatible writer behavior:

- per-object IDs start from `10000`
- `mobjectIDcounter` keeps headroom (`objectCount + 10000`)

## Parser/Serializer Validation Findings

- TV4L `tree` blob is a recursive stream and can be parsed fully with zero tail when parsed with correct node/leaf rules.
- Parser should decode all node/blob variants and preserve raw payload for diagnostics.
- Writer must serialize from decoded semantic data, not by reusing raw blobs.

## TV4P Note

TV4P carries references and layer-selection state (for example active layer pointers).  
TV4L correctness alone is not enough for full TB behavior if TV4P linkage is wrong.

## Practical TB Symptoms Mapped to Root Causes

- **Missing templates / wrong template selected**:
  - wrong signedness in TML hash text, or hash clamping/collision.
- **Objects rendered off map / not selectable / require tree rebuild**:
  - wrong object-layer root bbox center generation.
- **Object count appears wrong or unstable**:
  - inner/root node count handling mismatch, pointer/reference mismatch, or invalid tree layout.

## Current Status (post-fixes)

- template hash path fixed (`TML signed`, `TV4L raw uint32`),
- root bbox generation corrected to georef-based center,
- inner node count field set to TB-compatible value,
- child order and object ID base aligned with observed TB output.

These changes resolved the previously reproduced “0 objects / rebuild needed / template mismatch” failure chain on tested `cup-cain` datasets.
