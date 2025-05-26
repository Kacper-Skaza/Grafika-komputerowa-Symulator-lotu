#define GLM_FORCE_RADIANS
#define GLM_FORCE_SWIZZLE
#define TINYOBJLOADER_IMPLEMENTATION

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <tiny_obj_loader.h>
#include "constants.h"
#include "lodepng.h"
#include "shaderprogram.h"

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>

ShaderProgram* sp = nullptr;

struct AirplaneState {
	glm::vec3 pos;
	float yaw, pitch;
	float speed;
};

AirplaneState airplane = { glm::vec3(0, 40, 0), 0.0f, 0.0f, 10.0f };
float yawRate = 0.0f;
float pitchRate = 0.0f;
float targetYawRate = 0.0f;
float currentYawRate = 0.0f;
const float yawAccel = glm::radians(180.0f);

float currentRollAngle = 0.0f;
const float rollAccel = glm::radians(60.0f);

float MIN_X = -500, MAX_X = 500;
float MIN_Z = -500, MAX_Z = 500;
float MIN_Y = 10.0f, MAX_Y = +200.0f;

float aspectRatio = 1;

void freeOpenGLProgram(GLFWwindow* w) {
	delete sp;
}

void error_callback(int e, const char* d) {
	std::cerr << "GLFW Error: " << d << "\n";
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	const float ANG_V = glm::radians(45.0f); // angular speed in radians/sec

	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_LEFT)   targetYawRate = ANG_V;  // bank left
		if (key == GLFW_KEY_RIGHT)  targetYawRate = -ANG_V;  // bank right
		if (key == GLFW_KEY_DOWN)   pitchRate = -ANG_V;  // ↓ nose up (climb)
		if (key == GLFW_KEY_UP)     pitchRate = ANG_V;  // ↑ nose down (descend)
	}
	else if (action == GLFW_RELEASE) {
		if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) targetYawRate = 0.0f;
		if (key == GLFW_KEY_DOWN || key == GLFW_KEY_UP)    pitchRate = 0.0f;
	}
}

void windowResizeCallback(GLFWwindow* w, int width, int height) {
	if (height == 0) return;
	aspectRatio = float(width) / float(height);
	glViewport(0, 0, width, height);
}

GLuint readTexture(const std::string& fname) {
	std::vector<unsigned char> img;
	unsigned w, h;
	auto err = lodepng::decode(img, w, h, fname);
	if (err) { std::cerr << "PNG decode error " << err << ": " << lodepng_error_text(err) << "\n"; return 0; }
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.data());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	// Dodane ustawienia wrap
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

GLuint makeColorTexture(float r, float g, float b, float a = 1.0f) {
	GLuint tex;
	unsigned char pixel[4] = {
		(unsigned char)(r * 255.0f),
		(unsigned char)(g * 255.0f),
		(unsigned char)(b * 255.0f),
		(unsigned char)(a * 255.0f)
	};
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(
		GL_TEXTURE_2D, 0, GL_RGBA,
		1, 1, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, pixel
	);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return tex;
}



// Globalne zmienne:
std::vector<std::vector<float>> vertsPerMatJet, normsPerMatJet, uvsPerMatJet;
std::vector<int> countsPerMatJet;
std::vector<tinyobj::material_t> materialsJet;
std::vector<GLuint> matTexIDsJet;

std::vector<std::vector<float>> vertsPerMatCity, normsPerMatCity, uvsPerMatCity;
std::vector<int> countsPerMatCity;
std::vector<tinyobj::material_t> materialsCity;
std::vector<GLuint> matTexIDsCity;



bool loadModel(
	const std::string& objFile,
	std::vector<std::vector<float>>& vertsPerMat,
	std::vector<std::vector<float>>& normsPerMat,
	std::vector<std::vector<float>>& uvsPerMat,
	std::vector<int>& countsPerMat,
	std::vector<tinyobj::material_t>& materials
) {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::string warn, err;

	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objFile.c_str(), ".");
	if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
	if (!err.empty()) std::cerr << "ERR : " << err << "\n";
	if (!ret) return false;

	int M = (int)materials.size();
	vertsPerMat.assign(M, {});
	normsPerMat.assign(M, {});
	uvsPerMat.assign(M, {});
	countsPerMat.assign(M, 0);

	for (auto& shape : shapes) {
		size_t index_offset = 0;
		for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
			int fv = shape.mesh.num_face_vertices[f];
			int matID = shape.mesh.material_ids[f];
			// Kontrola matID
			if (matID < 0 || matID >= M) matID = 0; // domyślny materiał

			for (int v = 0; v < fv; v++) {
				auto idx = shape.mesh.indices[index_offset + v];

				vertsPerMat[matID].push_back(attrib.vertices[3 * idx.vertex_index + 0]);
				vertsPerMat[matID].push_back(attrib.vertices[3 * idx.vertex_index + 1]);
				vertsPerMat[matID].push_back(attrib.vertices[3 * idx.vertex_index + 2]);
				vertsPerMat[matID].push_back(1.0f);

				if (idx.normal_index >= 0) {
					normsPerMat[matID].push_back(attrib.normals[3 * idx.normal_index + 0]);
					normsPerMat[matID].push_back(attrib.normals[3 * idx.normal_index + 1]);
					normsPerMat[matID].push_back(attrib.normals[3 * idx.normal_index + 2]);
					normsPerMat[matID].push_back(0.0f);
				}
				else {
					normsPerMat[matID].insert(normsPerMat[matID].end(), { 0,1,0,0 });
				}

				if (idx.texcoord_index >= 0) {
					uvsPerMat[matID].push_back(attrib.texcoords[2 * idx.texcoord_index + 0]);
					uvsPerMat[matID].push_back(attrib.texcoords[2 * idx.texcoord_index + 1]);
				}
				else {
					uvsPerMat[matID].insert(uvsPerMat[matID].end(), { 0,0 });
				}
				countsPerMat[matID] += 1;
			}
			index_offset += fv;
		}
	}
	return true;
}

bool initOpenGLProgram(GLFWwindow* window) {
	glClearColor(0.15f, 0.15f, 0.25f, 1);
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glfwSetWindowSizeCallback(window, windowResizeCallback);
	glfwSetKeyCallback(window, keyCallback);

		if (!loadModel("jetanima.obj", vertsPerMatJet, normsPerMatJet, uvsPerMatJet, countsPerMatJet, materialsJet)) {
		std::cerr << "Failed to load jetanima\n";
		return false;
	}
	if (!loadModel("City.obj", vertsPerMatCity, normsPerMatCity, uvsPerMatCity, countsPerMatCity, materialsCity)) {
		std::cerr << "Failed to load city\n";
		return false;
	}

	matTexIDsJet.resize(materialsJet.size(), 0);
	for (size_t i = 0; i < materialsJet.size(); i++) {
		std::string texname = materialsJet[i].diffuse_texname;
		auto pos = texname.find_last_of("/\\");
		if (pos != std::string::npos) texname = texname.substr(pos + 1);
		if (!texname.empty()) {
			matTexIDsJet[i] = readTexture(texname);
		}
	}

	matTexIDsCity.resize(materialsCity.size());
	for (size_t i = 0; i < materialsCity.size(); i++) {
		auto& mat = materialsCity[i];
		// jeśli jest tekstura – wczytaj ją
		if (!mat.diffuse_texname.empty()) {
			std::string texname = mat.diffuse_texname;
			auto pos = texname.find_last_of("/\\");
			if (pos != std::string::npos) texname = texname.substr(pos + 1);
			matTexIDsCity[i] = readTexture(texname);
			if (matTexIDsCity[i] == 0) {
				// błąd wczytania → fallback na kolor
				matTexIDsCity[i] = makeColorTexture(
					mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.dissolve
				);
			}
		}
		else {
			// brak pliku – stwórz 1×1 texturę z kolorem Kd
			matTexIDsCity[i] = makeColorTexture(
				mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.dissolve
			);
		}
	}

	glm::vec2 cityMin(FLT_MAX, FLT_MAX);
	glm::vec2 cityMax(-FLT_MAX, -FLT_MAX);

	for (const auto& matVerts : vertsPerMatCity) {
		// matVerts is a flat [x,y,z,1, x,y,z,1, …]
		for (size_t i = 0; i + 3 < matVerts.size(); i += 4) {
			float x = matVerts[i];
			float z = matVerts[i + 2];
			cityMin.x = glm::min(cityMin.x, x);
			cityMax.x = glm::max(cityMax.x, x);
			cityMin.y = glm::min(cityMin.y, z);
			cityMax.y = glm::max(cityMax.y, z);
		}
	}

	// store these in globals
	MIN_X = cityMin.x;
	MAX_X = cityMax.x;
	MIN_Z = cityMin.y;
	MAX_Z = cityMax.y;

	sp = new ShaderProgram("v_simplest.glsl", nullptr, "f_simplest.glsl");
	return true;
}


void updatePhysics(float dt) {
	// 1) Smooth yawRate
	float diff = targetYawRate - currentYawRate;
	float maxStep = yawAccel * dt;
	diff = glm::clamp(diff, -maxStep, maxStep);
	currentYawRate += diff;

	// 2) Apply to heading
	airplane.yaw += currentYawRate * dt;

	// 3) Clamp pitch as before
	airplane.pitch += pitchRate * dt;
	airplane.pitch = glm::clamp(airplane.pitch, glm::radians(-45.0f), glm::radians(45.0f));

	// 4) Desired roll based on currentYawRate
	float desiredRoll = glm::clamp(-currentYawRate * 0.5f,
		glm::radians(-30.0f),
		glm::radians(30.0f));
	// 5) Smooth rollAngle
	float rollDiff = desiredRoll - currentRollAngle;
	float maxRollStep = rollAccel * dt;
	rollDiff = glm::clamp(rollDiff, -maxRollStep, maxRollStep);
	currentRollAngle += rollDiff;

	// 6) Build rotation matrix with smooth roll
	glm::mat4 R(1.0f);
	R = glm::rotate(R, airplane.yaw, glm::vec3(0, 1, 0));
	R = glm::rotate(R, airplane.pitch, glm::vec3(1, 0, 0));
	R = glm::rotate(R, currentRollAngle, glm::vec3(0, 0, 1));

	// 7) Move forward
	glm::vec3 forward = glm::normalize(glm::vec3(R * glm::vec4(0, 0, 1, 0)));
	airplane.pos += forward * (airplane.speed * dt);

	// 8) Clamp position...
	airplane.pos.x = glm::clamp(airplane.pos.x, MIN_X, MAX_X);
	airplane.pos.z = glm::clamp(airplane.pos.z, MIN_Z, MAX_Z);
	airplane.pos.y = glm::clamp(airplane.pos.y, MIN_Y, MAX_Y);
}


void drawModel(
	const std::vector<std::vector<float>>& vertsPerMat,
	const std::vector<std::vector<float>>& normsPerMat,
	const std::vector<std::vector<float>>& uvsPerMat,
	const std::vector<int>& countsPerMat,
	const std::vector<GLuint>& matTexIDs
) {
	for (size_t m = 0; m < matTexIDs.size(); m++) {
		GLuint tex = matTexIDs[m];
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glUniform1i(sp->u("textureMap0"), 0);

		glEnableVertexAttribArray(sp->a("vertex"));
		glVertexAttribPointer(sp->a("vertex"), 4, GL_FLOAT, GL_FALSE, 0, vertsPerMat[m].data());

		glEnableVertexAttribArray(sp->a("normal"));
		glVertexAttribPointer(sp->a("normal"), 4, GL_FLOAT, GL_FALSE, 0, normsPerMat[m].data());

		glEnableVertexAttribArray(sp->a("texCoord0"));
		glVertexAttribPointer(sp->a("texCoord0"), 2, GL_FLOAT, GL_FALSE, 0, uvsPerMat[m].data());

		glDrawArrays(GL_TRIANGLES, 0, countsPerMat[m]);

		glDisableVertexAttribArray(sp->a("vertex"));
		glDisableVertexAttribArray(sp->a("normal"));
		glDisableVertexAttribArray(sp->a("texCoord0"));
	}
}

void drawScene(GLFWwindow* window) {
	glClearColor(0.2f, 0.5f, 1.0f, 1.0f); // Niebo
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glm::mat4 R(1.0f);
	R = glm::rotate(R, airplane.yaw, glm::vec3(0, 1, 0));
	R = glm::rotate(R, airplane.pitch, glm::vec3(1, 0, 0));
	float rollAngle = glm::clamp(-yawRate * 0.5f,
		glm::radians(-30.0f),
		glm::radians(30.0f));
	R = glm::rotate(R, rollAngle, glm::vec3(0, 0, 1));

	// model matrix for the airplane
	glm::mat4 M = glm::translate(glm::mat4(1.0f), airplane.pos)
		* glm::rotate(glm::mat4(1.0f), airplane.yaw, glm::vec3(0, 1, 0))
		* glm::rotate(glm::mat4(1.0f), airplane.pitch, glm::vec3(1, 0, 0))
		* glm::rotate(glm::mat4(1.0f), currentRollAngle, glm::vec3(0, 0, 1));

	// recalc forward (−Z) after rotation
	glm::vec3 forward = glm::normalize(glm::vec3(R * glm::vec4(0, 0, 1, 0)));
	glm::vec3 worldUp = glm::vec3(0, 1, 0);

	// place camera behind & above the plane
	glm::vec3 camPos = airplane.pos - forward * 12.0f + worldUp * 4.0f;

	glm::mat4 V = glm::lookAt(camPos, airplane.pos, worldUp);
	glm::mat4 P = glm::perspective(glm::radians(60.0f),
		aspectRatio,
		0.1f, 2000.0f);

	glUniform4f(sp->u("lp"), -1.0f, 1.0f, -0.5f, 0.0f);


	sp->use();
	glUniformMatrix4fv(sp->u("P"), 1, GL_FALSE, glm::value_ptr(P));
	glUniformMatrix4fv(sp->u("V"), 1, GL_FALSE, glm::value_ptr(V));

	// draw airplane
	glUniformMatrix4fv(sp->u("M"), 1, GL_FALSE, glm::value_ptr(M));
	drawModel(vertsPerMatJet, normsPerMatJet, uvsPerMatJet,
		countsPerMatJet, matTexIDsJet);

	// draw city with identity
	glm::mat4 I(1.0f);
	glUniformMatrix4fv(sp->u("M"), 1, GL_FALSE, glm::value_ptr(I));
	drawModel(vertsPerMatCity, normsPerMatCity, uvsPerMatCity,
		countsPerMatCity, matTexIDsCity);

	glfwSwapBuffers(window);
}



// ===== MAIN =====
int main() {
	glfwSetErrorCallback(error_callback);
	if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
	GLFWwindow* w = glfwCreateWindow(800, 600, "Symulator lotu", nullptr, nullptr);
	if (!w) { glfwTerminate(); return 1; }
	glfwMakeContextCurrent(w);
	glfwSwapInterval(1);
	if (glewInit() != GLEW_OK) { std::cerr << "GLEW init failed\n"; return 1; }

	if (!initOpenGLProgram(w)) return 1;

	glfwSetTime(0);
	while (!glfwWindowShouldClose(w)) {
		float dt = glfwGetTime();
		glfwSetTime(0);

		updatePhysics(dt);

		drawScene(w);
		glfwPollEvents();
	}
	freeOpenGLProgram(w);
	glfwDestroyWindow(w);
	glfwTerminate();
	return 0;
}
