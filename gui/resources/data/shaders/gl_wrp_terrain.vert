#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aHeight;
layout(location=2) in float aMask;
layout(location=3) in vec3 aSat;
layout(location=4) in vec3 aNormal;
uniform mat4 uMVP;
out float vHeight;
out float vMask;
out vec3 vSat;
out vec2 vWorldXZ;
out vec3 vNormalWS;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vHeight = aHeight;
    vMask = aMask;
    vSat = aSat;
    vWorldXZ = vec2(aPos.x, aPos.z);
    vNormalWS = normalize(aNormal);
}
