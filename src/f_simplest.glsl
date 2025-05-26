#version 330

uniform sampler2D textureMap0;

in vec4 l;
in vec4 n;
in vec4 v;
in vec2 iTexCoord0;

out vec4 pixelColor;

void main() {
    vec3 ml = normalize(l.xyz);
    vec3 mn = normalize(n.xyz);
    vec3 mv = normalize(v.xyz);
    vec3 mr = reflect(-ml, mn);

    float nl = clamp(dot(mn, ml), 0.0, 1.0);
    float rv = pow(clamp(dot(mr, mv), 0.0, 1.0), 25.0);

    vec4 kd = texture(textureMap0, iTexCoord0);
    vec4 ks = kd * 0.5;

    vec4 ambient = 0.3 * kd;
    pixelColor = ambient + vec4(nl * kd.rgb, kd.a) + vec4(ks.rgb * rv, 0.0);
}