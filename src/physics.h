#ifndef AIRPLANE_PHYSICS_H
#define AIRPLANE_PHYSICS_H

#include <glm/glm.hpp>

struct AirplaneState {
    glm::vec3 position;
    glm::vec3 velocity;
    float pitch, yaw, roll;
    float speed;
};

void updateAirplane(AirplaneState& state, float dt, float pitchInput, float yawInput, float rollInput, float throttleInput);

#endif
