#version 330

uniform sampler2D textureMap0; //globalnie
uniform sampler2D textureMap1; //globalnie

out vec4 pixelColor; //Zmienna wyjsciowa fragment shadera. Zapisuje sie do niej ostateczny (prawie) kolor piksela
in vec4 iC;

in vec4 l;
in vec4 n;
in vec4 v;
in vec2 iTexCoord0; //globalnie
in vec2 iTexCoord1; //globalnie

void main(void) {


	vec4 ml = normalize(l);
	vec4 mn = normalize(n);
	vec4 mv = normalize(v);
	vec4 mr=reflect(-ml,mn); //Wektor odbity

	float nl = clamp(dot(mn, ml), 0, 1); //Kosinus kąta pomiędzy wektorami n i l.
	float rv = pow(clamp(dot(mr, mv), 0, 1), 25); // Kosinus kąta pomiędzy wektorami r i v podniesiony do 25 potęgi

	vec4 kamuflaz=texture(textureMap0,iTexCoord0);
	vec4 metal=texture(textureMap1,iTexCoord1);

	vec4 kd=mix(metal, kamuflaz, 0.3);
	vec4 ks=kd/2;

	vec4 ambient = 0.3 * kd;
	pixelColor = ambient + vec4(nl * kd.rgb, kd.a) + vec4(ks.rgb*rv, 0);

	//pixelColor= vec4(nl * kd.rgb, kd.a) + vec4(ks.rgb*rv, 0); //Wyliczenie modelu oświetlenia (bez ambient);
}
