#version 120

attribute vec4 vertex;
attribute vec4 normal;
attribute vec2 texCoord0;

uniform mat4 P;
uniform mat4 V;
uniform mat4 M;

varying vec3 fragNormal;
varying vec3 fragPos;
varying vec2 fragUV;

void main() {
    fragNormal = normalize(mat3(M) * normal.xyz);
    fragPos = vec3(M * vertex);
    fragUV = texCoord0;
    gl_Position = P * V * M * vertex;
}
