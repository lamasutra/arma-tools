
#version 330 core
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
uniform sampler2D uTexture;
uniform sampler2D uNormalMap;
uniform sampler2D uSpecularMap;
uniform bool uHasTexture;
uniform bool uHasNormalMap;
uniform bool uHasSpecularMap;
uniform vec3 uLightDir;
uniform bool uHasMaterial;
uniform vec3 uMatAmbient;
uniform vec3 uMatDiffuse;
uniform vec3 uMatEmissive;
uniform vec3 uMatSpecular;
uniform float uMatSpecPower;
uniform int uShaderMode;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNormal);
    if (uHasNormalMap) {
        vec3 t = normalize(vTangent - dot(vTangent, n) * n);
        vec3 b = normalize(cross(n, t));
        vec3 nt = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
        n = normalize(mat3(t, b, n) * nt);
    }
    vec3 v = vec3(0.0, 0.0, 1.0);
    float diff = max(dot(n, -uLightDir), 0.0);
    float back_fill = max(dot(n, uLightDir), 0.0);
    vec4 baseColor = uHasTexture ? texture(uTexture, vUV) : vec4(0.7, 0.7, 0.7, 1.0);
    if (uShaderMode == 3 && baseColor.a < 0.35) discard;

    vec3 ambient = clamp(uHasMaterial ? uMatAmbient : vec3(0.18), 0.0, 1.0);
    vec3 diffuseC = clamp(uHasMaterial ? uMatDiffuse : vec3(1.0), 0.0, 1.0);
    vec3 emissive = clamp(uHasMaterial ? uMatEmissive : vec3(0.0), 0.0, 1.0);
    vec3 specC = clamp(uHasMaterial ? uMatSpecular : vec3(0.08), 0.0, 1.0);
    float sp = uHasMaterial ? max(2.0, uMatSpecPower) : 32.0;

    vec3 h = normalize(-uLightDir + v);
    float spec = pow(max(dot(n, h), 0.0), sp);
    if (uHasSpecularMap)
        spec *= dot(texture(uSpecularMap, vUV).rgb, vec3(0.3333));
    float light = min(1.0, 0.15 + 0.85 * diff + 0.20 * back_fill);

    if (uShaderMode == 1) {
        spec *= 1.8;
        light = min(1.0, light * 1.08);
    } else if (uShaderMode == 2) {
        emissive *= 1.6;
    }

    vec3 lit = baseColor.rgb * (ambient * 0.25 + diffuseC * light)
             + specC * spec * 0.35
             + emissive;
    FragColor = vec4(clamp(lit, 0.0, 1.0), baseColor.a);
    if (FragColor.a < 0.01) discard;
}
