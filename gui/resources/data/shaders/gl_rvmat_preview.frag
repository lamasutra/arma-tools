#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vTangent;
in vec2 vUV1;
uniform sampler2D uTexDiffuse;
uniform sampler2D uTexNormal;
uniform sampler2D uTexSpec;
uniform sampler2D uTexAO;
uniform bool uHasDiffuse;
uniform bool uHasNormal;
uniform bool uHasSpec;
uniform bool uHasAO;
uniform vec3 uLightDir;
uniform vec3 uCamPos;
uniform vec3 uMatAmbient;
uniform vec3 uMatDiffuse;
uniform vec3 uMatEmissive;
uniform vec3 uMatSpecular;
uniform float uMatSpecPower;
uniform mat3 uUvDiffuse;
uniform mat3 uUvNormal;
uniform mat3 uUvSpec;
uniform mat3 uUvAO;
uniform int uUvSourceDiffuse;
uniform int uUvSourceNormal;
uniform int uUvSourceSpec;
uniform int uUvSourceAO;
uniform int uViewMode;
uniform bool uDiffuseIsSRGB;
out vec4 FragColor;
void main() {
    vec2 uvBaseDiff = (uUvSourceDiffuse == 1) ? vUV1 : vUV;
    vec2 uvBaseNrm = (uUvSourceNormal == 1) ? vUV1 : vUV;
    vec2 uvBaseSpec = (uUvSourceSpec == 1) ? vUV1 : vUV;
    vec2 uvBaseAO = (uUvSourceAO == 1) ? vUV1 : vUV;
    vec2 uvD = (uUvDiffuse * vec3(uvBaseDiff, 1.0)).xy;
    vec2 uvN = (uUvNormal * vec3(uvBaseNrm, 1.0)).xy;
    vec2 uvS = (uUvSpec * vec3(uvBaseSpec, 1.0)).xy;
    vec2 uvA = (uUvAO * vec3(uvBaseAO, 1.0)).xy;
    vec3 baseN = normalize(vNormal);
    vec3 t = normalize(vTangent - dot(vTangent, baseN) * baseN);
    vec3 b = normalize(cross(baseN, t));
    if (!gl_FrontFacing) {
        baseN = -baseN;
        t = -t;
        b = -b;
    }
    vec3 n = baseN;
    if (uHasNormal) {
        vec3 nTex = texture(uTexNormal, uvN).xyz * 2.0 - 1.0;
        n = normalize(mat3(t, b, baseN) * nTex);
    }

    vec3 baseColor = uHasDiffuse ? texture(uTexDiffuse, uvD).rgb : vec3(0.7);
    if (uDiffuseIsSRGB) baseColor = pow(baseColor, vec3(2.2));
    vec3 ambient = clamp(uMatAmbient, 0.0, 1.0);
    vec3 diffuseC = clamp(uMatDiffuse, 0.0, 1.0);
    vec3 emissive = clamp(uMatEmissive, 0.0, 1.0);
    vec3 specC = clamp(uMatSpecular, 0.0, 1.0);
    float sp = max(2.0, uMatSpecPower);

    float diff = max(dot(n, uLightDir), 0.0);
    float backFill = max(dot(n, -uLightDir), 0.0) * 0.20;
    vec3 v = normalize(uCamPos - vWorldPos);
    vec3 h = normalize(uLightDir + v);
    float spec = pow(max(dot(n, h), 0.0), sp);
    float specMask = 1.0;
    if (uHasSpec) specMask = dot(texture(uTexSpec, uvS).rgb, vec3(0.3333));
    vec3 aoColor = uHasAO ? texture(uTexAO, uvA).rgb : vec3(1.0);

    vec3 lit = baseColor * (ambient * 0.25 + diffuseC * min(1.0, diff + backFill))
             + specC * spec * specMask * 0.35
             + emissive;
    vec3 outColor = lit;
    if (uViewMode == 1) {
        outColor = baseColor;
        outColor = pow(clamp(outColor, 0.0, 1.0), vec3(1.0 / 2.2));
    } else if (uViewMode == 2) {
        outColor = n * 0.5 + 0.5;
    } else if (uViewMode == 3) {
        outColor = uHasSpec ? texture(uTexSpec, uvS).rgb : vec3(0.5);
    } else if (uViewMode == 4) {
        outColor = aoColor;
    } else {
        outColor = pow(clamp(outColor, 0.0, 1.0), vec3(1.0 / 2.2));
    }
    FragColor = vec4(clamp(outColor, 0.0, 1.0), 1.0);
}
