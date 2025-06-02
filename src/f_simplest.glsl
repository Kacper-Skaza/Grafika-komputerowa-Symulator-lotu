#version 330

uniform sampler2D textureMap0;

in vec4 l_point; // punktowe
in vec4 l_sun;   // kierunkowe
in vec4 n;
in vec4 v;
in vec2 iTexCoord0;

out vec4 pixelColor;

void main() {
    vec3 N = normalize(n.xyz);
    vec3 V = normalize(v.xyz);

    // Punktowe
    vec3 L1 = normalize(l_point.xyz);
    vec3 R1 = reflect(-L1, N);
    float diff1 = max(dot(N, L1), 0.0);
    float spec1 = pow(max(dot(R1, V), 0.0), 25.0);

    // Kierunkowe
    vec3 L2 = normalize(l_sun.xyz);
    vec3 R2 = reflect(-L2, N);
    float diff2 = max(dot(N, L2), 0.0);
    float spec2 = pow(max(dot(R2, V), 0.0), 25.0);

    vec4 kd = texture(textureMap0, iTexCoord0);
    vec4 ks = kd * 0.5;

    vec4 ambient = 0.3 * kd;
    vec4 diffuse = kd * (diff1 + diff2);
    vec4 specular = ks * (spec1 + spec2);

    pixelColor = ambient + diffuse + vec4(specular.rgb, kd.a);
}
