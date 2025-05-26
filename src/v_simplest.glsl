#version 330

uniform mat4 P;
uniform mat4 V;
uniform mat4 M;
uniform vec4 lp;

in vec4 vertex;
in vec4 color;
in vec4 normal;
in vec2 texCoord0;

out vec4 iC;
out vec4 l;
out vec4 n;
out vec4 v;
out vec2 iTexCoord0;

void main(void) {
    l = normalize(V * lp); // SUN
    n = normalize(V * M * normal); // normalny
    v = normalize(vec4(0,0,0,1) - V * M * vertex); // do obserwatora
    iC = color;
    iTexCoord0 = texCoord0;

    gl_Position = P * V * M * vertex;
}
