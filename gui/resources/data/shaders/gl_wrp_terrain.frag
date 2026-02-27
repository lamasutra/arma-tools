#version 330 core
in float vHeight;
in float vMask;
in vec3 vSat;
in vec2 vWorldXZ;
in vec3 vNormalWS;
uniform float uMinH;
uniform float uMaxH;
uniform int uMode;
uniform sampler2D uTextureIndex;
uniform sampler2D uMaterialLookup;
uniform sampler2D uLayerAtlas0;
uniform sampler2D uLayerAtlas1;
uniform sampler2D uLayerAtlas2;
uniform sampler2D uLayerAtlas3;
uniform sampler2D uLayerAtlas4;
uniform sampler2D uLayerAtlas5;
uniform sampler2D uLayerAtlas6;
uniform sampler2D uLayerAtlas7;
uniform sampler2D uLayerAtlas8;
uniform sampler2D uLayerAtlas9;
uniform sampler2D uLayerAtlas10;
uniform sampler2D uLayerAtlas11;
uniform sampler2D uLayerAtlas12;
uniform sampler2D uLayerAtlas13;
uniform int uMaterialLookupRows;
uniform float uTextureCellSize;
uniform int uTextureGridW;
uniform int uTextureGridH;
uniform bool uHasTextureIndex;
uniform bool uHasMaterialLookup;
uniform vec2 uCameraXZ;
uniform float uMaterialMidDistance;
uniform float uMaterialFarDistance;
uniform bool uShowPatchBounds;
uniform bool uShowTileBounds;
uniform bool uShowLodTint;
uniform vec4 uPatchBounds;
uniform vec3 uPatchLodColor;
uniform float uTileCellSize;
uniform int uPatchLod;
uniform int uSamplerCount;
uniform int uDebugMode;
uniform int uSeamDebugMode;
uniform float uTerrainMaxZ;
uniform bool uFlipTerrainZ;
out vec4 FragColor;

#ifndef SURFACE_CAP
#define SURFACE_CAP 4
#endif

#ifndef QUALITY_TIER
#define QUALITY_TIER 2
#endif

#ifndef HAS_NORMALS
#define HAS_NORMALS 1
#endif

#ifndef HAS_MACRO
#define HAS_MACRO 1
#endif

vec3 hash_color(float n) {
    uint h = uint(max(n, 0.0));
    h ^= (h >> 16);
    h *= 0x7feb352du;
    h ^= (h >> 15);
    h *= 0x846ca68bu;
    h ^= (h >> 16);
    float r = float((h >> 0) & 255u) / 255.0;
    float g = float((h >> 8) & 255u) / 255.0;
    float b = float((h >> 16) & 255u) / 255.0;
    return vec3(0.20 + 0.75 * r, 0.20 + 0.75 * g, 0.20 + 0.75 * b);
}

vec4 sample_layer(int role, vec2 uv) {
    if (role == 0) return texture(uLayerAtlas0, uv);
    if (role == 1) return texture(uLayerAtlas1, uv);
    if (role == 2) return texture(uLayerAtlas2, uv);
    if (role == 3) return texture(uLayerAtlas3, uv);
    if (role == 4) return texture(uLayerAtlas4, uv);
    if (role == 5) return texture(uLayerAtlas5, uv);
    if (role == 6) return texture(uLayerAtlas6, uv);
    if (role == 7) return texture(uLayerAtlas7, uv);
    if (role == 8) return texture(uLayerAtlas8, uv);
    if (role == 9) return texture(uLayerAtlas9, uv);
    if (role == 10) return texture(uLayerAtlas10, uv);
    if (role == 11) return texture(uLayerAtlas11, uv);
    if (role == 12) return texture(uLayerAtlas12, uv);
    if (role == 13) return texture(uLayerAtlas13, uv);
    return vec4(0.0);
}

vec4 sample_slot(int role, vec4 slot, vec2 uv01) {
    if (slot.z <= 0.0 || slot.w <= 0.0) return vec4(0.0);
    vec2 uv = slot.xy + fract(uv01) * slot.zw;
    return sample_layer(role, uv);
}

vec3 decode_normal(vec3 packed_n) {
    vec3 n = packed_n * 2.0 - 1.0;
    n.z = max(0.001, n.z);
    return normalize(n);
}

void main() {
    vec3 base_normal = normalize(vNormalWS);
    vec3 c;
    if (uMode == 3) {
        c = vSat;
    } else if (uMode == 2) {
        vec3 tex_color = vSat;
        int desired = -1;
        float camera_dist = distance(vWorldXZ, uCameraXZ);
        float lookup_z = uFlipTerrainZ ? (uTerrainMaxZ - vWorldXZ.y) : vWorldXZ.y;
        vec2 lookup_xz = vec2(vWorldXZ.x, lookup_z);
        vec2 world_uv = lookup_xz / max(uTextureCellSize, 0.0001);
        if (uHasTextureIndex && uTextureGridW > 0 && uTextureGridH > 0) {
            float cell = max(uTextureCellSize, 0.0001);
            vec2 tile_coord = lookup_xz / cell;
            ivec2 gi = ivec2(floor(tile_coord));
            gi = clamp(gi, ivec2(0), ivec2(uTextureGridW - 1, uTextureGridH - 1));
            desired = int(floor(texelFetch(uTextureIndex, gi, 0).r + 0.5));

            vec2 local_uv = tile_coord - vec2(gi);
            vec2 uv_sat = local_uv;
            vec2 uv_mask = local_uv;
            vec2 uv_tex0 = world_uv * 0.35;
            vec2 uv_tex1 = world_uv * 0.55;
            vec2 uv_tex2 = world_uv * 1.25;

            vec4 meta = texelFetch(uMaterialLookup, ivec2(desired, 0), 0);
            int surface_count = clamp(int(floor(meta.x + 0.5)), 0, SURFACE_CAP);
            bool layered = (meta.y > 0.5) && (surface_count > 0);
            vec4 sat_slot = texelFetch(uMaterialLookup, ivec2(desired, 1), 0);
            vec4 mask_slot = texelFetch(uMaterialLookup, ivec2(desired, 2), 0);
            vec3 sat = sample_slot(0, sat_slot, uv_sat).rgb;
            if (sat_slot.z <= 0.0 || sat_slot.w <= 0.0) sat = vSat;
            tex_color = sat;

            if (QUALITY_TIER > 0 && layered && camera_dist <= uMaterialFarDistance) {
                vec4 raw_mask = sample_slot(1, mask_slot, uv_mask);
                vec4 weights = raw_mask;
                float wsum = max(dot(weights, vec4(1.0)), 0.0001);
                weights /= wsum;
                vec3 near_color = vec3(0.0);
                vec3 near_normal = base_normal;
                for (int i = 0; i < SURFACE_CAP; ++i) {
                    if (i >= surface_count) break;
                    int row_base = 3 + i * 3;
                    vec4 macro_slot = texelFetch(uMaterialLookup, ivec2(desired, row_base + 0), 0);
                    vec4 normal_slot = texelFetch(uMaterialLookup, ivec2(desired, row_base + 1), 0);
                    vec4 detail_slot = texelFetch(uMaterialLookup, ivec2(desired, row_base + 2), 0);

                    vec3 detail = sample_slot(2 + i * 3 + 2, detail_slot, uv_tex2).rgb;
                    vec3 macro = vec3(1.0);
#if QUALITY_TIER >= 2 && HAS_MACRO
                    vec3 sampled_macro = sample_slot(2 + i * 3 + 0, macro_slot, uv_tex0).rgb;
                    if (macro_slot.z > 0.0 && macro_slot.w > 0.0) macro = sampled_macro;
#endif
                    vec3 nrm = base_normal;
#if QUALITY_TIER >= 2 && HAS_NORMALS
                    if (normal_slot.z > 0.0 && normal_slot.w > 0.0) {
                        vec3 detail_n = decode_normal(sample_slot(2 + i * 3 + 1, normal_slot, uv_tex1).xyz);
                        nrm = normalize(base_normal + vec3(detail_n.x, detail_n.y - 1.0, detail_n.z));
                    }
#endif
                    vec3 surface_color = detail * macro;
                    near_color += weights[i] * surface_color;
                    near_normal += weights[i] * nrm;
                }

                vec3 n = normalize(near_normal);
                float ndotl = clamp(dot(n, normalize(vec3(0.22, 0.95, 0.18))), 0.0, 1.0);
                near_color *= (0.72 + ndotl * 0.28);
                float t = clamp((camera_dist - uMaterialMidDistance)
                                / max(1.0, uMaterialFarDistance - uMaterialMidDistance), 0.0, 1.0);
                tex_color = mix(near_color, sat, t);

                if (uDebugMode == 1) {
                    tex_color = sat;
                } else if (uDebugMode == 2) {
                    tex_color = raw_mask.rgb;
                } else if (uDebugMode >= 3 && uDebugMode < (3 + SURFACE_CAP)) {
                    int sidx = uDebugMode - 3;
                    if (sidx < surface_count) {
                        int row_base = 3 + sidx * 3;
                        vec4 detail_slot = texelFetch(uMaterialLookup, ivec2(desired, row_base + 2), 0);
                        tex_color = sample_slot(2 + sidx * 3 + 2, detail_slot, uv_tex2).rgb;
                    }
                }
            }
        }

        if (desired >= 0 && desired < 65535) {
            c = tex_color;
        } else if (desired >= 0) {
            c = vSat;
        } else {
            c = vec3(0.35, 0.0, 0.35);
        }
    } else if (uMode == 1) {
        int cls = int(vMask + 0.5);
        if (cls == 1) c = vec3(0.70, 0.60, 0.35);
        else if (cls == 2) c = vec3(0.92, 0.86, 0.55);
        else if (cls == 3) c = vec3(0.16, 0.38, 0.72);
        else if (cls == 4) c = vec3(0.12, 0.46, 0.14);
        else if (cls == 5) c = vec3(0.25, 0.25, 0.25);
        else c = vec3(0.45, 0.36, 0.22);
    } else {
        float denom = max(0.001, uMaxH - uMinH);
        float t = clamp((vHeight - uMinH) / denom, 0.0, 1.0);
        vec3 low = vec3(0.10, 0.35, 0.12);
        vec3 mid = vec3(0.55, 0.45, 0.25);
        vec3 high = vec3(0.90, 0.90, 0.88);
        c = t < 0.5 ? mix(low, mid, t * 2.0) : mix(mid, high, (t - 0.5) * 2.0);
    }

    if (uSeamDebugMode == 1) {
        c = vec3(0.64);
    } else if (uSeamDebugMode == 2) {
        c = base_normal * 0.5 + 0.5;
    }

    if (uShowLodTint) {
        c = mix(c, uPatchLodColor, 0.25);
    }

    if (uShowTileBounds && uTileCellSize > 0.0) {
        vec2 tile_uv = fract(vWorldXZ / uTileCellSize);
        float edge = min(min(tile_uv.x, 1.0 - tile_uv.x), min(tile_uv.y, 1.0 - tile_uv.y));
        float line = 1.0 - smoothstep(0.0, 0.03, edge);
        c = mix(c, vec3(0.0, 0.0, 0.0), line * 0.55);
    }

    if (uShowPatchBounds) {
        float dx = min(abs(vWorldXZ.x - uPatchBounds.x), abs(uPatchBounds.z - vWorldXZ.x));
        float dz = min(abs(vWorldXZ.y - uPatchBounds.y), abs(uPatchBounds.w - vWorldXZ.y));
        float edge = min(dx, dz);
        float line = 1.0 - smoothstep(0.0, 3.0, edge);
        c = mix(c, vec3(0.95, 0.15, 0.15), line * 0.8);
    }

    FragColor = vec4(c, 1.0);
}
