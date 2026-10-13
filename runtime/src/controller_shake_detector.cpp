// The shake detector on its own, without SDL, for runtime/tests/controller_shake_tests.cpp.
#include "controller_shake.h"

#include <cmath>

namespace ControllerShake {

namespace {

constexpr double kStandardGravity = 9.80665;
// How quickly the gravity estimate follows the accelerometer: slowly with a gyroscope keeping it
// turned with the controller, faster without one.
constexpr double kGravitySecondsWithGyro = 0.5;
constexpr double kGravitySecondsWithoutGyro = 0.2;

bool Finite3(const float* v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

// Turns `v` by `angle` radians about the unit axis `k` (Rodrigues).
void Rotate(double v[3], const double k[3], double angle) {
    const double c = std::cos(angle), s = std::sin(angle);
    const double cross[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2], k[0] * v[1] - k[1] * v[0]};
    const double dot = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
    for (int i = 0; i < 3; ++i) v[i] = v[i] * c + cross[i] * s + k[i] * dot * (1.0 - c);
}

} // namespace

bool Detector::Sample(const float accel[3], const float* gyro, double seconds) noexcept {
    if (accel == nullptr || !Finite3(accel) || !std::isfinite(seconds)) {
        return false;
    }
    const bool useGyro = gyro != nullptr && Finite3(gyro);
    const double dt = seconds - m_lastTime;
    if (!m_haveGravity || dt <= 0.0 || dt > kMaxGapSeconds) {
        for (int i = 0; i < 3; ++i) m_gravity[i] = accel[i];
        m_haveGravity = true;
        m_lastTime = seconds;
        m_armed = false;
        m_lastMagnitude = 0.0;
        return false;
    }
    m_lastTime = seconds;

    // A direction fixed in the room turns the opposite way to the controller.
    if (useGyro) {
        const double rate = std::sqrt(double(gyro[0]) * gyro[0] + double(gyro[1]) * gyro[1] +
                                      double(gyro[2]) * gyro[2]);
        if (rate > 1e-6) {
            const double axis[3] = {-gyro[0] / rate, -gyro[1] / rate, -gyro[2] / rate};
            Rotate(m_gravity, axis, rate * dt);
        }
    }
    double linear[3];
    for (int i = 0; i < 3; ++i) linear[i] = accel[i] - m_gravity[i];
    const double magnitude =
        std::sqrt(linear[0] * linear[0] + linear[1] * linear[1] + linear[2] * linear[2]) / kStandardGravity;
    m_lastMagnitude = magnitude;
    const double ease = dt / ((useGyro ? kGravitySecondsWithGyro : kGravitySecondsWithoutGyro) + dt);
    for (int i = 0; i < 3; ++i) m_gravity[i] += ease * linear[i];

    if (magnitude <= kRearmG) {
        m_armed = true;
        return false;
    }
    if (!m_armed || magnitude < kTriggerG) {
        return false;
    }
    m_armed = false;
    if (m_lastTrigger >= 0.0 && seconds - m_lastTrigger < kCooldownSeconds) {
        return false;
    }
    m_lastTrigger = seconds;
    return true;
}

void Detector::Reset() noexcept {
    *this = Detector{};
}

} // namespace ControllerShake
