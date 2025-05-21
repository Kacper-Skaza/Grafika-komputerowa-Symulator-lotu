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
#include "physics.h"

#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>

float speed_x = 0, speed_y = 0;
float aspectRatio = 1;


std::vector<std::vector<float>>  vertsPerMat;
std::vector<std::vector<float>>  normsPerMat;
std::vector<std::vector<float>>  uvsPerMat;
std::vector<int>                 countsPerMat;

std::vector<tinyobj::material_t> materials;
std::vector<GLuint>              matTexIDs;

GLuint metalTex, skyTex;

ShaderProgram* sp = nullptr;

AirplaneState airplane = { glm::vec3(0,0,0), glm::vec3(0,0,0), 0,0,0, 2.0f };


void error_callback(int e, const char* d) { std::cerr << "GLFW Error: " << d << "\n"; }


void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_LEFT) speed_y = -PI / 2;
		if (key == GLFW_KEY_RIGHT) speed_y = PI / 2;
		if (key == GLFW_KEY_UP) speed_x = PI / 2;
		if (key == GLFW_KEY_DOWN) speed_x = -PI / 2;
	}
	if (action == GLFW_RELEASE) {
		if (key == GLFW_KEY_LEFT) speed_y = 0;
		if (key == GLFW_KEY_RIGHT) speed_y = 0;
		if (key == GLFW_KEY_UP) speed_x = 0;
		if (key == GLFW_KEY_DOWN) speed_x = 0;
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
	return tex;
}


bool loadModel(const std::string& objFile) {
	tinyobj::attrib_t                attrib;
	std::vector<tinyobj::shape_t>    shapes;
	std::string warn, err;

	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objFile.c_str(), ".");
	if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
	if (!err.empty())  std::cerr << "ERR : " << err << "\n";
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

	if (!loadModel("jetanima.obj")) { std::cerr << "Failed to load model\n"; return false; }

	matTexIDs.resize(materials.size(), 0);
	for (size_t i = 0; i < materials.size(); i++) {
		std::string texname = materials[i].diffuse_texname;

		// Cut path (Windows/Linux)
		auto pos = texname.find_last_of("/\\");
		if (pos != std::string::npos) texname = texname.substr(pos + 1);

		if (texname.empty()) {
			if (materials[i].name == "cam")            texname = "atlasjet-black.png"; // окно
			else if (materials[i].name == "kantuc")    texname = "wide.png";
			else if (materials[i].name == "uçak")      texname = "atlasjet-white.png";
			else if (materials[i].name == "uçak.001")  texname = "atlasjet-black.png";
			else if (materials[i].name == "motor")     texname = "metal.jpg";
			else if (materials[i].name == "motor_red") texname = "metal.jpg";
			else if (materials[i].name == "matel")     texname = "metal.jpg";
			else texname = "";
		}

		GLuint texID = 0;
		//if (!texname.empty()) texID = readTexture(texname);

		if (!texname.empty()) {
			std::cout << "Trying to load texture: " << texname << std::endl;
			texID = readTexture(texname);
		}
		matTexIDs[i] = texID;

		std::cout << "Mat[" << i << "][" << materials[i].name << "] -> " << texname << " id=" << texID << "\n";
	}

	metalTex = readTexture("metal.jpg");
	//skyTex = readTexture("sky.png");

	sp = new ShaderProgram("v_simplest.glsl", nullptr, "f_simplest.glsl");
	//sp = new ShaderProgram("v_texture.glsl", nullptr, "f_texture.glsl");
	return true;
}

void freeOpenGLProgram(GLFWwindow* w) {
	delete sp;
}

void drawScene(GLFWwindow* window, float angle_x, float angle_y) {
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glm::mat4 V = glm::lookAt(
		glm::vec3(0, 0, -12),
		glm::vec3(0, 0, 0),
		glm::vec3(0, 1, 0)
	);
	glm::mat4 P = glm::perspective(
		glm::radians(45.0f),
		aspectRatio, 0.1f, 200.0f
	);
	glm::mat4 M(1.0f);
	M = glm::rotate(M, angle_y, glm::vec3(1, 0, 0));
	M = glm::rotate(M, angle_x, glm::vec3(0, 1, 0));

	sp->use();
	glUniformMatrix4fv(sp->u("P"), 1, GL_FALSE, glm::value_ptr(P));
	glUniformMatrix4fv(sp->u("V"), 1, GL_FALSE, glm::value_ptr(V));
	glUniformMatrix4fv(sp->u("M"), 1, GL_FALSE, glm::value_ptr(M));
	glUniform4f(sp->u("lp"), 10.0f, 10.0f, 0.0f, 1.0f);


	for (size_t m = 0; m < materials.size(); m++) {
		GLuint tex = matTexIDs[m];

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glUniform1i(sp->u("textureMap0"), 0);

		glActiveTexture(GL_TEXTURE1);
		glBindTexture(GL_TEXTURE_2D, metalTex);
		glUniform1i(sp->u("textureMap1"), 1);

		// далее - всё как есть
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

	glfwSwapBuffers(window);
}

int main() {
	glfwSetErrorCallback(error_callback);
	if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
	GLFWwindow* w = glfwCreateWindow(800, 600, "Jet", nullptr, nullptr);
	if (!w) { glfwTerminate();return 1; }
	glfwMakeContextCurrent(w);
	glfwSwapInterval(1);
	if (glewInit() != GLEW_OK) { std::cerr << "GLEW init failed\n";return 1; }

	if (!initOpenGLProgram(w)) return 1;

	float ax = 0, ay = 0;
	glfwSetTime(0);
	while (!glfwWindowShouldClose(w)) {
		float pitchInput = 0, yawInput = 0, rollInput = 0, throttleInput = 0;
		ax += speed_x * glfwGetTime();
		ay += speed_y * glfwGetTime();
		float dt = glfwGetTime();
		glfwSetTime(0);
		drawScene(w, ay, ax);
		glfwPollEvents();
	}
	freeOpenGLProgram(w);
	glfwDestroyWindow(w);
	glfwTerminate();
	return 0;
}
