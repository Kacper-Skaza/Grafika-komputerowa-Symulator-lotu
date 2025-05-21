#version 120

uniform sampler2D textureMap0;      // твоя основная diffuse/албедо текстура
uniform int useTexture;
uniform vec3 diffuseColor;

varying vec3 fragNormal;
varying vec3 fragPos;
varying vec2 fragUV;

void main() {
    // Имитация источника света (можно вынести в uniform!)
    vec3 lightPos = vec3(20, 30, -40); // сбоку, сверху, спереди
    vec3 viewPos = vec3(0, 0, -12);    // камера

    // Базовый цвет — либо из текстуры, либо из материала
    vec4 baseColor = useTexture != 0 ? texture2D(textureMap0, fragUV) : vec4(diffuseColor, 1.0);

    // ----- Phong освещение -----
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(lightPos - fragPos);
    vec3 V = normalize(viewPos - fragPos);
    vec3 R = reflect(-L, N);

    // Матовая составляющая
    float diff = max(dot(N, L), 0.0);

    float spec = pow(max(dot(R, V), 0.0), 88.0);

    float brightness = dot(baseColor.rgb, vec3(0.333));
    float metalness = brightness > 0.6 ? 1.0 : 0.0;

    vec3 finalColor = baseColor.rgb * (0.35 + diff * 0.8);
    finalColor += metalness * vec3(1.0, 1.0, 0.97) * spec * 0.75;
    gl_FragColor = vec4(finalColor, baseColor.a);
}
