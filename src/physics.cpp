#include "physics.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

void updateAirplane(AirplaneState& state, float dt, float pitchInput, float yawInput, float rollInput, float throttleInput) {
    // Update orientation
    state.pitch += pitchInput * dt;
    state.yaw += yawInput * dt;
    state.roll += rollInput * dt;

    // Clamp pitch and roll for realism
    if (state.pitch > glm::radians(80.0f)) state.pitch = glm::radians(80.0f);
    if (state.pitch < glm::radians(-80.0f)) state.pitch = glm::radians(-80.0f);
    if (state.roll > glm::radians(80.0f)) state.roll = glm::radians(80.0f);
    if (state.roll < glm::radians(-80.0f)) state.roll = glm::radians(-80.0f);

    // Throttle
    state.speed += throttleInput * dt;
    if (state.speed < 0.1f) state.speed = 0.1f;
    if (state.speed > 10.0f) state.speed = 10.0f;

    // Calculate forward direction
    glm::mat4 R = glm::yawPitchRoll(state.yaw, state.pitch, state.roll);
    glm::vec3 forward = glm::vec3(R * glm::vec4(0, 0, 1, 0));

    // Update position
    state.velocity = forward * state.speed;
    state.position += state.velocity * dt;
}
