#version 330

uniform mat4 P;
uniform mat4 V;
uniform mat4 M;
uniform vec4 sun; // kierunek œwiat³a (w = 0)
uniform vec4 lp;  // pozycja œwiat³a (w = 1)

in vec4 vertex;
in vec4 color;
in vec4 normal;
in vec2 texCoord0;

out vec4 iC;
out vec4 l_sun;
out vec4 l_point;
out vec4 n;
out vec4 v;
out vec2 iTexCoord0;

void main(void) {
    vec4 vertex_eye = V * M * vertex;

    l_point = normalize(V * lp - vertex_eye);   // punktowe
    l_sun = normalize(V * sun);                 // kierunkowe (wektor)

    n = normalize(V * M * normal);              // normalny
    v = normalize(vec4(0,0,0,1) - vertex_eye);  // wektor do kamery

    iC = color;
    iTexCoord0 = texCoord0;

    gl_Position = P * vertex_eye;
}
