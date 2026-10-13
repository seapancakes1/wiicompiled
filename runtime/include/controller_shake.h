#pragma once

// Shake for Trick on controllers with motion sensors (DualSense, DualShock 4, Switch Pro, Joy-Con):
// a sharp shake presses D-pad Up, the GameCube trick, for a moment. Wii Remotes don't need it (the
// game reads their shakes itself through KPAD). Off unless the Mac app turns it on for a port.

#include <cstdint>

struct PADStatus;

namespace ControllerShake {

// Turns one sharp movement into one trick. Samples are the controller's accelerometer in m/s²
// (gravity included) and, when it has one, its gyroscope in rad/s, both in SDL's sensor frame.
// Gravity is tracked by turning the last estimate with the gyroscope and easing it towards the
// accelerometer; what's left over is the shake. Like the phones (KartPadMotionSteering, the phone
// page): a trick takes 1.35 g, the controller has to settle below 0.35 g before the next, and an
// impulse within 0.45 s of the last trick is used up without one.
class Detector {
public:
    static constexpr double kRearmG = 0.35;
    static constexpr double kTriggerG = 1.35;
    static constexpr double kCooldownSeconds = 0.45;
    // A longer gap (the game paused, the controller just connected) starts over: gravity is
    // measured again and the controller has to settle first.
    static constexpr double kMaxGapSeconds = 0.25;

    // True when this sample is a trick.
    bool Sample(const float accel[3], const float* gyro, double seconds) noexcept;
    void Reset() noexcept;
    // The last sample's movement in g, without gravity (for tests and the log).
    double LastMagnitude() const noexcept { return m_lastMagnitude; }

private:
    double m_gravity[3] = {};
    bool m_haveGravity = false;
    double m_lastTime = 0.0;
    bool m_armed = false;
    double m_lastTrigger = -1.0;
    double m_lastMagnitude = 0.0;
};

// How long a trick holds D-pad Up.
inline constexpr double kPressSeconds = 0.15;

// Mixes tricks into a freshly read status set (PAD__Read_HLE, after the bindings). Turns each
// enabled port's sensors on, and back off when shake is turned off or the controller changes.
void Apply(PADStatus* statuses) noexcept;

} // namespace ControllerShake

// The Mac app sets this for the player each controller is on (KPControllerSettings tick).
extern "C" void PAD_HLE_SetPortShakeTricks(uint32_t chan, bool enabled);
