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

struct AABB {
	glm::vec3 min;
	glm::vec3 max;
};
std::vector<AABB> cityBuildings;

struct AirplaneState {
	glm::vec3 pos;
	float yaw, pitch;
	float speed;
};

bool throttleUpPressed = false;
bool throttleDownPressed = false;

AirplaneState airplane = { glm::vec3(0, 40, 0), 0.0f, 0.0f, 10.0f };
float yawRate = 0.0f;
float pitchRate = 0.0f;
float targetYawRate = 0.0f;
float currentYawRate = 0.0f;
const float yawAccel = glm::radians(180.0f);

float currentRollAngle = 0.0f;
const float rollAccel = glm::radians(60.0f);

bool explosionActive = false;
float explosionTimer = 0.0f;
glm::vec3 explosionPos;
const float explosionDuration = 1.5f;

GLuint explosionTexture = 0;
const int explosionFramesX = 5;
const int explosionFramesY = 5;
const int explosionTotalFrames = explosionFramesX * explosionFramesY;

float verticalSpeed = 0.0f;
const float GRAVITY = 9.81f;
bool isStalling = false;


const float MIN_TAKEOFF_SPEED = 10.0f;
const float STALL_SPEED = 8.0f;
const float MAX_SPEED = 20.0f;

float throttle = 0.0f;
float targetThrottle = 0.0f;

bool onGround = true;

bool isLandingAssistActive = false;
float landingAssistTimer = 0.0f;
const float LANDING_ASSIST_DURATION = 2.0f;
const float SAFE_LANDING_SPEED = 10.0f;
const float LANDING_PITCH_THRESHOLD = glm::radians(25.0f);

AABB airportAABB;
glm::vec3 airportCenter;
std::vector<AABB> airportRunwayAABBs;
std::vector<AABB> airportObstacles;
glm::vec3 airportDrawOffset(-186.0f, 0.1f, 67.0f);
float airportGroundLevel = airportAABB.min.y + airportDrawOffset.y;
const float AIRPORT_SAFE_RADIUS = 30.0f;
const float STALL_NOSEDOWN = glm::radians(55.0f);
const float STALL_BLEND_SPEED = 2.5f;

float MIN_X = -10.0f, MAX_X = 10.0f;
float MIN_Z = -10.0f, MAX_Z = 10.0f;
float MIN_Y = 1.0f, MAX_Y = 200.0f;

float aspectRatio = 1;

void freeOpenGLProgram(GLFWwindow* w) {
	delete sp;
}

void error_callback(int e, const char* d) {
	std::cerr << "GLFW Error: " << d << "\n";
}

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	constexpr float ANG_V = glm::radians(20.0f); // angular speed in radians/sec
	constexpr float ANG_H = glm::radians(60.0f); // angular speed in radians/sec

	if (action == GLFW_PRESS) {
		if (key == GLFW_KEY_LEFT)   targetYawRate = ANG_H;  // bank left
		if (key == GLFW_KEY_RIGHT)  targetYawRate = -ANG_H;  // bank right

		if (airplane.speed >= MIN_TAKEOFF_SPEED) {
			if (key == GLFW_KEY_DOWN)   pitchRate = -ANG_V;
			if (key == GLFW_KEY_UP)     pitchRate = ANG_V;
		}
	}
	else if (action == GLFW_RELEASE) {
		if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) targetYawRate = 0.0f;
		if (key == GLFW_KEY_DOWN || key == GLFW_KEY_UP)    pitchRate = 0.0f;
	}

	if (key == GLFW_KEY_W) {
		if (action == GLFW_PRESS)
			throttleUpPressed = true;
		else if (action == GLFW_RELEASE)
			throttleUpPressed = false;
	}
	if (key == GLFW_KEY_S) {
		if (action == GLFW_PRESS)
			throttleDownPressed = true;
		else if (action == GLFW_RELEASE)
			throttleDownPressed = false;
	}
}

bool isOverAirport(const glm::vec3& posWorld) {
	glm::vec2 p2d(posWorld.x, posWorld.z);
	glm::vec2 a2d = glm::vec2(
		airportCenter.x + airportDrawOffset.x,
		airportCenter.z + airportDrawOffset.z
	);
	bool over = glm::distance(p2d, a2d) < AIRPORT_SAFE_RADIUS;

	return over;
}

bool isOnRunway(const glm::vec3& posWorld) {
	glm::vec3 posLocal = posWorld - airportDrawOffset;

	for (const auto& box : airportRunwayAABBs) {
		if (posLocal.x > box.min.x && posLocal.x < box.max.x &&
			posLocal.y >(box.min.y - 2.0f) && posLocal.y < (box.max.y + 3.0f) &&
			posLocal.z > box.min.z && posLocal.z < box.max.z) {

			return true;
		}
	}
	return false;
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

std::vector<std::vector<float>> vertsPerMatAirport, normsPerMatAirport, uvsPerMatAirport;
std::vector<int> countsPerMatAirport;
std::vector<tinyobj::material_t> materialsAirport;
std::vector<GLuint> matTexIDsAirport;



bool loadModel(
	const std::string& objFile,
	std::vector<std::vector<float>>& vertsPerMat,
	std::vector<std::vector<float>>& normsPerMat,
	std::vector<std::vector<float>>& uvsPerMat,
	std::vector<int>& countsPerMat,
	std::vector<tinyobj::material_t>& materials,
	AABB* outAABB = nullptr
) {
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::string warn, err;

	bool ret = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, objFile.c_str(), ".");
	if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
	if (!err.empty()) std::cerr << "ERR : " << err << "\n";
	if (!ret) return false;

	std::cout << "Loaded " << shapes.size() << " shapes from " << objFile << std::endl;

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
			if (matID < 0 || matID >= M) matID = 0;

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

	glm::vec3 min(FLT_MAX), max(-FLT_MAX);
	for (auto& shape : shapes) {
		size_t index_offset = 0;
		int matID = -1;
		if (!shape.mesh.material_ids.empty())
			matID = shape.mesh.material_ids[0];
		std::string matName = (matID >= 0 && matID < materials.size()) ? materials[matID].name : "";

		glm::vec3 shapeMin(FLT_MAX), shapeMax(-FLT_MAX);
		for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
			int fv = shape.mesh.num_face_vertices[f];
			for (int v = 0; v < fv; v++) {
				auto idx = shape.mesh.indices[index_offset + v];
				glm::vec3 vtx(
					attrib.vertices[3 * idx.vertex_index + 0],
					attrib.vertices[3 * idx.vertex_index + 1],
					attrib.vertices[3 * idx.vertex_index + 2]
				);
				shapeMin = glm::min(shapeMin, vtx);
				shapeMax = glm::max(shapeMax, vtx);
				min = glm::min(min, vtx);
				max = glm::max(max, vtx);
			}
			index_offset += fv;
		}

		bool isRunway = false;
		if (matName.find("Asphalt") != std::string::npos ||
			matName.find("runway") != std::string::npos ||
			matName.find("White") != std::string::npos ||
			matName.find("Blue") != std::string::npos) {
			isRunway = true;
		}

		if (isRunway)
			airportRunwayAABBs.push_back({ shapeMin, shapeMax });
		else
			airportObstacles.push_back({ shapeMin, shapeMax });
	}

	if (outAABB) *outAABB = { min, max };
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
		std::cerr << "Failed to load jetanima.obj\n";
		return false;
	}
	if (!loadModel("City.obj", vertsPerMatCity, normsPerMatCity, uvsPerMatCity, countsPerMatCity, materialsCity)) {
		std::cerr << "Failed to load City.obj\n";
		return false;
	}

	tinyobj::attrib_t attribCity;
	std::vector<tinyobj::shape_t> shapesCity;
	std::vector<tinyobj::material_t> matsCity;
	std::string warnCity, errCity;
	bool ok = tinyobj::LoadObj(&attribCity, &shapesCity, &matsCity, &warnCity, &errCity, "City.obj", ".");
	if (!ok) {
		std::cerr << "WARN: cannot reload City.obj for AABB generation\n";
	}
	else {
		for (auto& shape : shapesCity) {
			glm::vec3 shapeMin(FLT_MAX), shapeMax(-FLT_MAX);
			size_t index_offset = 0;
			for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
				int fv = shape.mesh.num_face_vertices[f];
				for (int v = 0; v < fv; v++) {
					auto idx = shape.mesh.indices[index_offset + v];
					glm::vec3 vtx(
						attribCity.vertices[3 * idx.vertex_index + 0],
						attribCity.vertices[3 * idx.vertex_index + 1],
						attribCity.vertices[3 * idx.vertex_index + 2]
					);
					shapeMin = glm::min(shapeMin, vtx);
					shapeMax = glm::max(shapeMax, vtx);
				}
				index_offset += fv;
			}
			cityBuildings.push_back({ shapeMin, shapeMax });
		}
	}


	if (!loadModel("Airport.obj", vertsPerMatAirport, normsPerMatAirport, uvsPerMatAirport, countsPerMatAirport, materialsAirport, &airportAABB)) {
		std::cerr << "Failed to load Airport.obj\n";
		return false;
	}

	airportCenter = (airportAABB.min + airportAABB.max) * 0.5f;
	airportGroundLevel = airportAABB.min.y;

	airplane.pos = airportCenter + airportDrawOffset + glm::vec3(0, 3.0f, 0);
	airplane.yaw = 0.0f;
	airplane.pitch = 0.0f;
	airplane.speed = 0.0f;
	onGround = true;
	throttle = 0.0f;
	targetThrottle = 0.0f;

	std::cout << "AIRPORT CENTER: " << airportCenter.x << " " << airportCenter.y << " " << airportCenter.z << std::endl;

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

	matTexIDsAirport.resize(materialsAirport.size());
	for (size_t i = 0; i < materialsAirport.size(); i++) {
		auto& mat = materialsAirport[i];
		// jeśli jest tekstura – wczytaj ją
		if (!mat.diffuse_texname.empty()) {
			std::string texname = mat.diffuse_texname;
			auto pos = texname.find_last_of("/\\");
			if (pos != std::string::npos) texname = texname.substr(pos + 1);
			matTexIDsAirport[i] = readTexture(texname);
			if (matTexIDsAirport[i] == 0) {
				// błąd wczytania → fallback na kolor
				matTexIDsAirport[i] = makeColorTexture(
					mat.diffuse[0], mat.diffuse[1], mat.diffuse[2], mat.dissolve
				);
			}
		}
		else {
			// brak pliku – stwórz 1×1 texturę z kolorem Kd
			matTexIDsAirport[i] = makeColorTexture(
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

	explosionTexture = readTexture("explosion.png");

	sp = new ShaderProgram("v_simplest.glsl", nullptr, "f_simplest.glsl");
	return true;
}

bool checkCollision(const glm::vec3& posWorld) {
	// Convert world coordinates back to airport‐local coordinates:
	glm::vec3 posLocal = posWorld - airportDrawOffset;

	// 1) If the plane is on the ground AND is exactly on a runway, skip collisions.
	if (onGround && isOnRunway(posWorld)) {
		return false;
	}

	// 1b) If the plane is on the ground but NOT on a runway, skip obstacle checks entirely
	//     (taxiing/parked). This prevents instant collision with airport structures.
	if (onGround) {
		return false;
	}

	// 2) If in the “safe radius” of the airport, do a more lenient check against obstacles
	if (isOverAirport(posWorld)) {
		float buffer = (airplane.speed < MIN_TAKEOFF_SPEED + 5.0f) ? 8.0f : 2.0f;
		for (const auto& box : airportObstacles) {
			// box.min/.max are in local coords
			bool inExtended =
				posLocal.x > (box.min.x - buffer) && posLocal.x < (box.max.x + buffer) &&
				posLocal.y >(box.min.y - buffer) && posLocal.y < (box.max.y + buffer) &&
				posLocal.z >(box.min.z - buffer) && posLocal.z < (box.max.z + buffer);

			bool inCore =
				posLocal.x > (box.min.x + 1.0f) && posLocal.x < (box.max.x - 1.0f) &&
				posLocal.y >(box.min.y + 1.0f) && posLocal.y < (box.max.y - 1.0f) &&
				posLocal.z >(box.min.z + 1.0f) && posLocal.z < (box.max.z - 1.0f);

			if (inExtended && inCore) {
				return true;
			}
		}
		return false;
	}

	// 3) Check against city buildings (these AABBs are already in world coords from City.obj)
	for (const auto& box : cityBuildings) {
		bool coll =
			posWorld.x > box.min.x && posWorld.x < box.max.x &&
			posWorld.y > box.min.y && posWorld.y < box.max.y &&
			posWorld.z > box.min.z && posWorld.z < box.max.z;

		if (coll) {
			return true;
		}
	}

	return false;
}


bool isInLandingApproach(const glm::vec3& posWorld) {
	for (const auto& box : airportRunwayAABBs) {
		float approachDistance = 50.0f;
		// Build an extended AABB around the runway to detect approach
		AABB extendedRunway = {
			glm::vec3(box.min.x - approachDistance, box.min.y - 10.0f, box.min.z - approachDistance),
			glm::vec3(box.max.x + approachDistance, box.max.y + 20.0f, box.max.z + approachDistance)
		};

		if (posWorld.x > extendedRunway.min.x && posWorld.x < extendedRunway.max.x &&
			posWorld.y > extendedRunway.min.y && posWorld.y < extendedRunway.max.y &&
			posWorld.z > extendedRunway.min.z && posWorld.z < extendedRunway.max.z) {
			return true;
		}
	}
	return false;
}

void updateLandingAssist(float dt) {
	if (!onGround && isInLandingApproach(airplane.pos)) {
		if (!isLandingAssistActive) {
			isLandingAssistActive = true;
			landingAssistTimer = 0.0f;
		}

		landingAssistTimer += dt;
		float assistStrength = glm::min(1.0f, landingAssistTimer / LANDING_ASSIST_DURATION);

		// Limit extreme pitch angles
		if (fabs(airplane.pitch) > LANDING_PITCH_THRESHOLD) {
			float targetPitch = glm::clamp(airplane.pitch, -LANDING_PITCH_THRESHOLD, LANDING_PITCH_THRESHOLD);
			airplane.pitch = glm::mix(airplane.pitch, targetPitch, assistStrength * 0.5f * dt);
		}

		// Limit extreme roll angles
		if (fabs(currentRollAngle) > glm::radians(15.0f)) {
			float maxLandingRoll = glm::radians(15.0f);
			float targetRoll = glm::clamp(currentRollAngle, -maxLandingRoll, maxLandingRoll);
			currentRollAngle = glm::mix(currentRollAngle, targetRoll, assistStrength * dt);
		}

		// If too fast on approach, gently slow down
		if (airplane.speed > SAFE_LANDING_SPEED) {
			float speedReduction = assistStrength * 3.0f * dt;
			airplane.speed = glm::max(SAFE_LANDING_SPEED, airplane.speed - speedReduction);
		}
	}
	else {
		isLandingAssistActive = false;
		landingAssistTimer = 0.0f;
	}
}


void drawExplosionSprite(float t, const glm::mat4& M) {
	int frame = int(t * explosionTotalFrames);
	if (frame >= explosionTotalFrames) frame = explosionTotalFrames - 1;

	int fx = frame % explosionFramesX;
	int fy = frame / explosionFramesX;

	float du = 1.0f / explosionFramesX;
	float dv = 1.0f / explosionFramesY;

	float u0 = fx * du;
	float v0 = fy * dv;
	float u1 = u0 + du;
	float v1 = v0 + dv;

	// Enable alpha blending
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Bind texture
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, explosionTexture);

	glUniform1i(sp->u("textureMap0"), 0);
	glUniformMatrix4fv(sp->u("M"), 1, GL_FALSE, glm::value_ptr(M));

	glEnableVertexAttribArray(sp->a("vertex"));
	glEnableVertexAttribArray(sp->a("texCoord0"));

	float quadVerts[] = {
		-1, -1, 0, 1,
		 1, -1, 0, 1,
		 1,  1, 0, 1,
		-1,  1, 0, 1
	};
	float quadUV[] = {
		u0, v1,
		u1, v1,
		u1, v0,
		u0, v0
	};

	glVertexAttribPointer(sp->a("vertex"), 4, GL_FLOAT, GL_FALSE, 0, quadVerts);
	glVertexAttribPointer(sp->a("texCoord0"), 2, GL_FLOAT, GL_FALSE, 0, quadUV);

	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

	glDisableVertexAttribArray(sp->a("vertex"));
	glDisableVertexAttribArray(sp->a("texCoord0"));
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_BLEND);
}


void startExplosion(const glm::vec3& pos) {
	explosionActive = true;
	explosionTimer = 0.0f;
	explosionPos = pos;
}

float takeoffTimer = 0.0f;

void updatePhysics(float dt) {
	if (explosionActive) {
		explosionTimer += dt;
		if (explosionTimer >= explosionDuration) {
			explosionActive = false;
			// Reset to airport
			airplane.pos = airportCenter + airportDrawOffset + glm::vec3(0, 3.0f, 0);
			airplane.yaw = 0;
			airplane.pitch = 0;
			currentYawRate = 0;
			targetYawRate = 0;
			yawRate = 0;
			pitchRate = 0;
			currentRollAngle = 0;
			throttle = 0.0f;
			targetThrottle = 0.0f;
			airplane.speed = 0.0f;
			onGround = true;
			verticalSpeed = 0.0f;
			isStalling = false;
			isLandingAssistActive = false;
		}
		return;
	}

	// Update landing assistance
	updateLandingAssist(dt);

	// Throttle handling
	const float throttleRate = 0.5f;
	if (throttleUpPressed)
		targetThrottle = glm::min(1.0f, targetThrottle + throttleRate * dt);
	if (throttleDownPressed)
		targetThrottle = glm::max(0.0f, targetThrottle - throttleRate * dt);

	// Smooth throttle change
	float thrDiff = targetThrottle - throttle;
	float maxThrStep = 1.5f * dt;
	if (fabs(thrDiff) < maxThrStep) throttle = targetThrottle;
	else throttle += glm::sign(thrDiff) * maxThrStep;
	throttle = glm::clamp(throttle, 0.0f, 1.0f);

	// Speed calculation
	float targetSpeed = throttle * MAX_SPEED;
	float spdDiff = targetSpeed - airplane.speed;
	float maxSpdStep = 7.0f * dt;
	if (fabs(spdDiff) < maxSpdStep) airplane.speed = targetSpeed;
	else airplane.speed += glm::sign(spdDiff) * maxSpdStep;
	airplane.speed = glm::clamp(airplane.speed, 0.0f, MAX_SPEED);

	// ----------- On Ground ----------
	if (onGround || airplane.pos.y == MIN_Y) {
		airplane.pos.y = airportGroundLevel + 2.0f;
		verticalSpeed = 0.0f;
		isStalling = false;

		if (pitchRate == 0.0f) {
			airplane.pitch = glm::mix(airplane.pitch, 0.0f, 0.1f);
		}
		else {
			airplane.pitch += pitchRate * dt;
			airplane.pitch = glm::clamp(airplane.pitch, glm::radians(-15.0f), glm::radians(15.0f));
		}

		// Improved takeoff conditions
		if (airplane.speed >= MIN_TAKEOFF_SPEED && airplane.pitch < glm::radians(-5.0f)) { // Nose up for takeoff
			onGround = false;
			takeoffTimer = 1.2f; // Give more time for takeoff transition
			verticalSpeed = 2.0f; // Initial upward velocity
			std::cout << "Taking off! Speed: " << airplane.speed << std::endl;
		}
	}

	// ---------- In Air -----------
	else {
		// Takeoff transition period - be extra careful about collisions
		if (takeoffTimer > 0.0f) {
			takeoffTimer -= dt;
			// Gradually increase altitude during takeoff
			airplane.pos.y = glm::max(airplane.pos.y, airportGroundLevel + 2.0f + (1.0f - takeoffTimer) * 5.0f);
		}

		// Free pitch control
		airplane.pitch += pitchRate * dt;
		airplane.pitch = glm::clamp(airplane.pitch, glm::radians(-45.0f), glm::radians(45.0f));

		// Stall handling
		if (airplane.speed < STALL_SPEED && airplane.pos.y > MIN_Y + 0.5f) {
			if (!isStalling) {
				isStalling = true;
			}
			airplane.pitch = glm::mix(airplane.pitch, STALL_NOSEDOWN, 1.0f - expf(-STALL_BLEND_SPEED * dt));
			verticalSpeed -= GRAVITY * dt;
			airplane.pos.y += verticalSpeed * dt;
		}
		else {
			if (isStalling) {
				isStalling = false;
				verticalSpeed = 0.0f;
			}
		}

		// Apply vertical movement for stalling
		if (isStalling) {
			glm::mat4 R(1.0f);
			R = glm::rotate(R, airplane.yaw, glm::vec3(0, 1, 0));
			R = glm::rotate(R, airplane.pitch, glm::vec3(1, 0, 0));
			glm::vec3 localDown = glm::normalize(glm::vec3(R * glm::vec4(0, -1, 0, 0)));
			airplane.pos += localDown * (-verticalSpeed * dt);
		}

	}

	// Yaw and roll control
	float diff = targetYawRate - currentYawRate;
	float maxStep = yawAccel * dt;
	diff = glm::clamp(diff, -maxStep, maxStep);
	currentYawRate += diff;
	airplane.yaw += currentYawRate * dt;

	// Roll
	float desiredRoll = glm::clamp(-currentYawRate * 0.5f,
		glm::radians(-30.0f), glm::radians(30.0f));
	float rollDiff = desiredRoll - currentRollAngle;
	float maxRollStep = rollAccel * dt;
	rollDiff = glm::clamp(rollDiff, -maxRollStep, maxRollStep);
	currentRollAngle += rollDiff;

	// Forward movement
	glm::mat4 R(1.0f);
	R = glm::rotate(R, airplane.yaw, glm::vec3(0, 1, 0));
	R = glm::rotate(R, airplane.pitch, glm::vec3(1, 0, 0));
	R = glm::rotate(R, currentRollAngle, glm::vec3(0, 0, 1));
	glm::vec3 forward = glm::normalize(glm::vec3(R * glm::vec4(0, 0, 1, 0)));
	airplane.pos += forward * (airplane.speed * dt);

	// Boundary checks (more lenient)
	bool outOfBounds = false;
	float boundary_buffer = 20.0f; // Give some extra space
	if (airplane.pos.x <= (MIN_X - boundary_buffer) || airplane.pos.x >= (MAX_X + boundary_buffer) ||
		airplane.pos.z <= (MIN_Z - boundary_buffer) || airplane.pos.z >= (MAX_Z + boundary_buffer)) {
		outOfBounds = true;
	}

	// Clamp position but don't explode immediately if near airport
	airplane.pos.x = glm::clamp(airplane.pos.x, MIN_X - boundary_buffer, MAX_X + boundary_buffer);
	airplane.pos.z = glm::clamp(airplane.pos.z, MIN_Z - boundary_buffer, MAX_Z + boundary_buffer);
	airplane.pos.y = glm::clamp(airplane.pos.y, MIN_Y, MAX_Y);

	// Only explode if really out of bounds and not near airport
	if (outOfBounds && !isOverAirport(airplane.pos)) {
		startExplosion(airplane.pos);
		return;
	}

	// Only check collision if not in takeoff transition
	if (takeoffTimer <= 0.0f && checkCollision(airplane.pos)) {
		startExplosion(airplane.pos);
		return;
	}
}


#include <chrono>
void displayFlightInfo() {
	static auto last = std::chrono::steady_clock::now();
	auto now = std::chrono::steady_clock::now();
	float dt = std::chrono::duration<float>(now - last).count();
	if (dt > 1.5f) {
		std::cout << "Speed & POS_Y: " << airplane.speed << ", " << airplane.pos.y
			<< " | Throttle: " << int(throttle * 100)
			<< " | " << (onGround ? "ON GROUND" : "IN AIR")
			<< std::endl;
		last = now;
	}
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

	// Sun
	glUniform4f(sp->u("lp"), -1.0f, 1.0f, -0.5f, 0.0f);

	sp->use();
	glUniformMatrix4fv(sp->u("P"), 1, GL_FALSE, glm::value_ptr(P));
	glUniformMatrix4fv(sp->u("V"), 1, GL_FALSE, glm::value_ptr(V));

	// draw City.obj
	glm::mat4 I(1.0f);
	glUniformMatrix4fv(sp->u("M"), 1, GL_FALSE, glm::value_ptr(I));
	drawModel(vertsPerMatCity, normsPerMatCity, uvsPerMatCity, countsPerMatCity, matTexIDsCity);

	// draw Airport.obj
	glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(-186.0f, 0.1f, 67.0f));
	glUniformMatrix4fv(sp->u("M"), 1, GL_FALSE, glm::value_ptr(T));
	drawModel(vertsPerMatAirport, normsPerMatAirport, uvsPerMatAirport, countsPerMatAirport, matTexIDsAirport);

	if (explosionActive) {
		float t = explosionTimer / explosionDuration;
		float scale = 1.5f;

		glm::mat4 model = glm::translate(glm::mat4(1.0f), explosionPos);
		// Биллбординг: квадраты будут всегда стоять к камере
		glm::vec3 camForward = glm::normalize(airplane.pos - camPos);
		glm::vec3 up = glm::vec3(0, 1, 0);
		glm::vec3 right = glm::normalize(glm::cross(up, camForward));
		up = glm::cross(camForward, right);
		glm::mat4 rot(1.0f);
		rot[0] = glm::vec4(right, 0);
		rot[1] = glm::vec4(up, 0);
		rot[2] = glm::vec4(camForward, 0);
		model = model * rot * glm::scale(glm::mat4(1.0f), glm::vec3(scale));

		sp->use();

		// Нарисовать 2 или 3 квадрата, повернутых друг к другу
		int crossCount = 6; // Можно сделать 3 для большей плотности
		for (int i = 0; i < crossCount; ++i) {
			float angle = glm::radians(30.0f * i); // 90° между двумя, 60° если 3
			glm::mat4 crossRot = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 1, 0));
			glm::mat4 M = model * crossRot;
			drawExplosionSprite(t, M);
		}
		displayFlightInfo();
		glfwSwapBuffers(window);
		return;
	}

	// draw airplane
	glUniformMatrix4fv(sp->u("M"), 1, GL_FALSE, glm::value_ptr(M));
	drawModel(vertsPerMatJet, normsPerMatJet, uvsPerMatJet,
		countsPerMatJet, matTexIDsJet);

	displayFlightInfo();

	glfwSwapBuffers(window);
}



// ===== MAIN =====
int main() {
	glfwSetErrorCallback(error_callback);
	if (!glfwInit()) { std::cerr << "GLFW init failed\n"; return 1; }
	GLFWwindow* w = glfwCreateWindow(1600, 1200, "Symulator lotu", nullptr, nullptr);
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
