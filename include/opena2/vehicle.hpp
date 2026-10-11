#pragma once

namespace opena2 {

struct VehicleControls {
    float throttle = 0.0F;
    float brake = 0.0F;
    float steering = 0.0F;
};

struct VehicleState {
    float x = 0.0F;
    float z = 0.0F;
    float heading = 0.0F;
    float speed = 0.0F;
};

void updateVehicle(
    VehicleState& vehicle,
    const VehicleControls& controls,
    float elapsedSeconds) noexcept;

} // namespace opena2
