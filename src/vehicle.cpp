#include "opena2/vehicle.hpp"

#include <algorithm>
#include <cmath>

namespace opena2 {
namespace {

constexpr float maximumForwardSpeed = 58.0F;
constexpr float maximumReverseSpeed = 10.0F;
constexpr float acceleration = 22.0F;
constexpr float braking = 38.0F;
constexpr float rollingDrag = 1.2F;
constexpr float lateralLimit = 26.0F;
constexpr float longitudinalLimit = 10000.0F;

float approachZero(float value, float amount) noexcept
{
    if (value > 0.0F) {
        return std::max(0.0F, value - amount);
    }
    return std::min(0.0F, value + amount);
}

} // namespace

void updateVehicle(
    VehicleState& vehicle,
    const VehicleControls& controls,
    float elapsedSeconds) noexcept
{
    if (!std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0F ||
        !std::isfinite(vehicle.x) || !std::isfinite(vehicle.z) ||
        !std::isfinite(vehicle.heading) || !std::isfinite(vehicle.speed) ||
        !std::isfinite(controls.throttle) || !std::isfinite(controls.brake) ||
        !std::isfinite(controls.steering)) {
        return;
    }

    const float delta = std::min(elapsedSeconds, 0.05F);
    const float throttle = std::clamp(controls.throttle, 0.0F, 1.0F);
    const float brake = std::clamp(controls.brake, 0.0F, 1.0F);
    const float steering = std::clamp(controls.steering, -1.0F, 1.0F);

    if (brake > 0.0F && std::abs(vehicle.speed) > 0.05F) {
        vehicle.speed = approachZero(vehicle.speed, braking * brake * delta);
    } else if (throttle > 0.0F) {
        const float drive = vehicle.speed < 0.0F ? braking : acceleration;
        vehicle.speed += drive * throttle * delta;
    } else {
        vehicle.speed = approachZero(vehicle.speed, rollingDrag * delta);
    }
    vehicle.speed = std::clamp(vehicle.speed, -maximumReverseSpeed, maximumForwardSpeed);

    const float steeringAuthority =
        0.16F + 0.84F * std::min(std::abs(vehicle.speed) / 12.0F, 1.0F);
    const float direction = vehicle.speed < -0.05F ? -1.0F : 1.0F;
    vehicle.heading += steering * steeringAuthority * direction * delta;
    constexpr float fullTurn = 6.28318530718F;
    if (vehicle.heading > fullTurn || vehicle.heading < -fullTurn) {
        vehicle.heading = std::remainder(vehicle.heading, fullTurn);
    }

    vehicle.x += std::sin(vehicle.heading) * vehicle.speed * delta;
    vehicle.z -= std::cos(vehicle.heading) * vehicle.speed * delta;
    if (vehicle.x < -lateralLimit) {
        vehicle.x = -lateralLimit;
        vehicle.speed = 0.0F;
    } else if (vehicle.x > lateralLimit) {
        vehicle.x = lateralLimit;
        vehicle.speed = 0.0F;
    }
    if (vehicle.z < -longitudinalLimit || vehicle.z > longitudinalLimit) {
        vehicle.z = std::clamp(vehicle.z, -longitudinalLimit, longitudinalLimit);
        vehicle.speed = 0.0F;
    }
}

} // namespace opena2
