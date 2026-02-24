# GUI Material Preview

This document describes how RVMat and texture stages are previewed in the GUI.

## Asset Browser RVMat Preview

When selecting an `.rvmat` file in **Asset Browser**:

- Left side shows parsed RVMat data (shader ids, colors, stages).
- Right side shows a live shaded sphere preview.

The sphere preview loads textures from RVMat stages using the asset index/DB:

- Diffuse: prefers `_mco`, then `_co`, then `_ca`.
- Normal: prefers `_nohq`.
- Specular: prefers `_smdi`.

Relative stage paths are resolved relative to the RVMat path.

## P3D Material Rendering

P3D preview uses RVMat material data when available:

- Face grouping uses texture path, with material path fallback.
- Material constants (ambient/diffuse/emissive/specular/specularPower) are applied.
- Additional maps from RVMat are supported:
  - Normal map (`_nohq`) via tangent-space shading.
  - Specular map (`_smdi`) as specular mask.

If no RVMat or stage textures are found, renderer falls back to base texture-only shading.

## Notes

- Procedural stage textures are ignored in preview.
- Common procedural stage textures are generated in preview (`#(...)`), including
  color, checker, noise/random, fresnel, alpha modulation, and normal vectors.
- Stage selection is heuristic-based by filename suffix.
- Preview focuses on practical visual diagnostics, not full engine parity.
