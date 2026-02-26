
#version 320 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec3 aTangent;
uniform mat4 uMVP;
uniform mat3 uNormalMat;
out vec3 vNormal;
out vec2 vUV;
out vec3 vTangent;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = normalize(uNormalMat * aNormal);
    vTangent = normalize(uNormalMat * aTangent);
    vUV = aUV;
}
