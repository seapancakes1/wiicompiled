#include "controller_shake.h"

#include "input_bindings.h"
#include "runtime_log.h"
#include "wii_remote_input.h"

#include <array>
#include <atomic>
#include <chrono>

#include <SDL3/SDL_gamepad.h>
#include <dolphin/pad.h>

namespace ControllerShake {

namespace {

using Clock = std::chrono::steady_clock;

std::array<std::atomic<bool>, PAD_CHANMAX> g_enabled{};

// The controller each port's shake is reading, and which of its sensors KartPad turned on.
struct Port {
    SDL_JoystickID id = 0;
    SDL_SensorType accel = SDL_SENSOR_INVALID;
    SDL_SensorType gyro = SDL_SENSOR_INVALID;
    bool ownsAccel = false;
    bool ownsGyro = false;
    Detector detector;
    Clock::time_point pressUntil{};
};
std::array<Port, PAD_CHANMAX> g_ports;

SDL_Gamepad* GamepadForPort(uint32_t port) {
    const s32 index = PADGetIndexForPort(port);
    SDL_Gamepad* gamepad = index < 0 ? nullptr : PADGetSDLGamepadForIndex(static_cast<u32>(index));
    // Wii Remotes shake through KPAD, and their own code keeps their accelerometers on.
    if (gamepad == nullptr ||
        WiiRemoteInput::KindForName(SDL_GetGamepadName(gamepad)) != WiiRemoteInput::Kind::NotWii) {
        return nullptr;
    }
    return gamepad;
}

void Release(Port& port) {
    if (port.id != 0) {
        if (SDL_Gamepad* gamepad = SDL_GetGamepadFromID(port.id)) {
            if (port.ownsAccel) SDL_SetGamepadSensorEnabled(gamepad, port.accel, false);
            if (port.ownsGyro) SDL_SetGamepadSensorEnabled(gamepad, port.gyro, false);
        }
    }
    port = Port{};
}

// Turns on the controller's accelerometer and gyroscope: the combined ones, else a Joy-Con pair's
// right then left.
void Acquire(Port& port, SDL_Gamepad* gamepad, uint32_t chan) {
    port.id = SDL_GetGamepadID(gamepad);
    constexpr std::array<std::array<SDL_SensorType, 2>, 3> kPairs = {{
        {SDL_SENSOR_ACCEL, SDL_SENSOR_GYRO},
        {SDL_SENSOR_ACCEL_R, SDL_SENSOR_GYRO_R},
        {SDL_SENSOR_ACCEL_L, SDL_SENSOR_GYRO_L},
    }};
    for (const auto& pair : kPairs) {
        if (!SDL_GamepadHasSensor(gamepad, pair[0])) continue;
        const auto enable = [&](SDL_SensorType sensor, bool& owns) {
            if (SDL_GamepadSensorEnabled(gamepad, sensor)) return true;
            owns = SDL_SetGamepadSensorEnabled(gamepad, sensor, true);
            return owns;
        };
        if (!enable(pair[0], port.ownsAccel)) {
            RT_LOG(RT_TAG_CONFIG) << "Shake for Trick on port " << (chan + 1)
                                  << ": could not turn on the accelerometer: " << SDL_GetError() << std::endl;
            return;
        }
        port.accel = pair[0];
        if (SDL_GamepadHasSensor(gamepad, pair[1]) && enable(pair[1], port.ownsGyro)) port.gyro = pair[1];
        RT_LOG(RT_TAG_CONFIG) << "Shake for Trick on port " << (chan + 1) << ": "
                              << (SDL_GetGamepadName(gamepad) ?: "controller")
                              << (port.gyro != SDL_SENSOR_INVALID ? " (with gyroscope)" : "") << std::endl;
        return;
    }
}

double Seconds(Clock::time_point time) {
    return std::chrono::duration<double>(time.time_since_epoch()).count();
}

} // namespace

void Apply(PADStatus* statuses) noexcept {
    if (statuses == nullptr) {
        return;
    }
    std::array<SDL_Gamepad*, PAD_CHANMAX> gamepads{};
    // Every port lets go first, so a controller moving between players is never turned off by the
    // one it left after the one it joined turned it on.
    for (uint32_t chan = 0; chan < PAD_CHANMAX; ++chan) {
        gamepads[chan] = g_enabled[chan].load(std::memory_order_relaxed) ? GamepadForPort(chan) : nullptr;
        if (g_ports[chan].id != (gamepads[chan] ? SDL_GetGamepadID(gamepads[chan]) : 0)) {
            Release(g_ports[chan]);
        }
    }
    const bool blocked = InputBindings::InputBlocked();
    const Clock::time_point now = Clock::now();
    for (uint32_t chan = 0; chan < PAD_CHANMAX; ++chan) {
        SDL_Gamepad* gamepad = gamepads[chan];
        Port& port = g_ports[chan];
        if (gamepad == nullptr) {
            continue;
        }
        if (port.id == 0) {
            Acquire(port, gamepad, chan);
        }
        float accel[3] = {}, gyro[3] = {};
        // An exact zero is SDL's buffer before the first report.
        if (port.accel == SDL_SENSOR_INVALID || !SDL_GetGamepadSensorData(gamepad, port.accel, accel, 3) ||
            (accel[0] == 0.0f && accel[1] == 0.0f && accel[2] == 0.0f)) {
            continue;
        }
        const bool haveGyro =
            port.gyro != SDL_SENSOR_INVALID && SDL_GetGamepadSensorData(gamepad, port.gyro, gyro, 3);
        // A shake while a KartPad screen is up is for nobody; the controller settles again after.
        if (blocked || statuses[chan].err != PAD_ERR_NONE) {
            port.detector.Reset();
            port.pressUntil = {};
            continue;
        }
        if (port.detector.Sample(accel, haveGyro ? gyro : nullptr, Seconds(now))) {
            port.pressUntil = now + std::chrono::duration_cast<Clock::duration>(
                                        std::chrono::duration<double>(kPressSeconds));
            RT_LOG(RT_TAG_HLE) << "Shake for Trick: port " << (chan + 1) << " at "
                               << port.detector.LastMagnitude() << " g" << std::endl;
        }
        if (now < port.pressUntil) {
            statuses[chan].button |= PAD_BUTTON_UP;
        }
    }
}

} // namespace ControllerShake

extern "C" void PAD_HLE_SetPortShakeTricks(uint32_t chan, bool enabled)
{
    if (chan < PAD_CHANMAX) {
        ControllerShake::g_enabled[chan].store(enabled, std::memory_order_relaxed);
    }
}
