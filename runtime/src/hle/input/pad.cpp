#include "hle_stubs.h"
#include "controller_shake.h"
#include "memory.h"
#include "hle/controller_status_contract.h"
#include "input_bindings.h"
#include "keyboard_defaults.h"
#include "wii_remote_input.h"
#include "wup028_adapter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_gamepad.h>
#include <dolphin/pad.h>

namespace {

std::atomic<bool> g_rumbleEnabled{true};
std::atomic<float> g_rumbleStrength{0.5f};
// A controller's own vibration strength (the Mac app sets it for the player it's on), 0 for off,
// or below 0 to follow Settings.
std::array<std::atomic<float>, PAD_CHANMAX> g_portRumbleStrength{-1.0f, -1.0f, -1.0f, -1.0f};

float RumbleStrength(uint32_t chan) {
    const float own = chan < PAD_CHANMAX ? g_portRumbleStrength[chan].load(std::memory_order_relaxed) : -1.0f;
    if (own >= 0.0f) {
        return own;
    }
    return g_rumbleEnabled.load(std::memory_order_relaxed) ? g_rumbleStrength.load(std::memory_order_relaxed) : 0.0f;
}

// The game turns each GameCube motor on and off. A pad with variable rumble gets the strength from
// Settings, for a short while that each frame renews while the motor is on, so a game that stops
// (an alert, the background pause) never leaves a controller vibrating. GameCube adapters keep
// Aurora's on/off motor commands.
using RumbleClock = std::chrono::steady_clock;
constexpr uint32_t kRumbleMs = 250;
constexpr auto kRumbleRenew = std::chrono::milliseconds(100);
std::array<bool, PAD_CHANMAX> g_motorOn{};
std::array<RumbleClock::time_point, PAD_CHANMAX> g_motorSent{};

SDL_Gamepad* VariableRumblePad(uint32_t chan) {
    if (chan >= PAD_CHANMAX || !PADSupportsRumbleIntensity(chan)) {
        return nullptr;
    }
    const int32_t index = PADGetIndexForPort(chan);
    return index < 0 ? nullptr : PADGetSDLGamepadForIndex(static_cast<uint32_t>(index));
}

void SendRumble(SDL_Gamepad* pad, uint32_t chan, bool on) {
    const auto level = static_cast<uint16_t>(std::lround(RumbleStrength(chan) * 65535.0f));
    SDL_RumbleGamepad(pad, on ? level : 0, on ? level : 0, on ? kRumbleMs : 0);
    g_motorSent[chan] = RumbleClock::now();
}

void RenewRumble() {
    const auto now = RumbleClock::now();
    for (uint32_t chan = 0; chan < PAD_CHANMAX; ++chan) {
        if (!g_motorOn[chan] || now - g_motorSent[chan] < kRumbleRenew) {
            continue;
        }
        if (SDL_Gamepad* pad = VariableRumblePad(chan)) {
            SendRumble(pad, chan, RumbleStrength(chan) > 0.0f);
        }
    }
}

bool NativeButtonHeld(SDL_Gamepad* gamepad, uint32_t nativeButton) {
    if (gamepad == nullptr || nativeButton == PAD_NATIVE_BUTTON_INVALID ||
        nativeButton >= SDL_GAMEPAD_BUTTON_COUNT) {
        return false;
    }
    return SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(nativeButton));
}

// A digital button bound to L or R has no analog travel of its own. On real
// hardware the click only engages at full depression, so report a full pull.
void FillTriggersHeldByButtons(PADStatus* statuses) {
    if (InputBindings::InputBlocked()) {
        return;
    }
    for (uint32_t port = 0; port < PAD_CHANMAX; ++port) {
        if (statuses[port].err != PAD_ERR_NONE) {
            continue;
        }
        const s32 index = PADGetIndexForPort(port);
        if (index < 0) {
            continue;
        }
        SDL_Gamepad* gamepad = PADGetSDLGamepadForIndex(static_cast<u32>(index));
        if (gamepad == nullptr) {
            continue;
        }
        const auto scan = [&](PADButtonMapping* mappings, u32 count) {
            if (mappings == nullptr) {
                return;
            }
            for (u32 i = 0; i < count; ++i) {
                const PADButtonMapping& mapping = mappings[i];
                if (mapping.padButton != PAD_TRIGGER_L && mapping.padButton != PAD_TRIGGER_R) {
                    continue;
                }
                if (!NativeButtonHeld(gamepad, mapping.nativeButton)) {
                    continue;
                }
                // Aurora leaves the L or R bit off while it keeps the press from the game (held
                // when a KartPad screen closed, or a pad sharing the keyboard's port); so does its pull.
                if ((statuses[port].button & mapping.padButton) == 0) {
                    continue;
                }
                if (mapping.padButton == PAD_TRIGGER_L) {
                    statuses[port].triggerLeft = 255;
                } else {
                    statuses[port].triggerRight = 255;
                }
            }
        };
        u32 count = 0;
        scan(PADGetButtonMappings(port, &count), count);
        count = 0;
        scan(PADGetAltButtonMappings(port, &count), count);
    }
}

void WritePadStatus(uint32_t base, const PADStatus& status) {
    const auto guestStatus = PadStatusContract::Encode({
        status.button,
        status.stickX,
        status.stickY,
        status.substickX,
        status.substickY,
        status.triggerL,
        status.triggerR,
        status.analogA,
        status.analogB,
        status.err,
    });
    uint8_t* dst = Memory::GetPointer(base, guestStatus.size());
    std::memcpy(dst, guestStatus.data(), guestStatus.size());
}

} // namespace

extern "C" void PAD_HLE_SetRumbleEnabled(bool enabled)
{
    g_rumbleEnabled.store(enabled, std::memory_order_relaxed);
}

extern "C" void PAD_HLE_SetRumbleStrength(float strength)
{
    g_rumbleStrength.store(std::clamp(strength, 0.0f, 1.0f), std::memory_order_relaxed);
}

extern "C" void PAD_HLE_SetPortRumbleStrength(uint32_t chan, float strength)
{
    if (chan < PAD_CHANMAX) {
        g_portRumbleStrength[chan].store(strength < 0.0f ? -1.0f : std::min(strength, 1.0f),
                                         std::memory_order_relaxed);
    }
}

extern "C" uint32_t PAD__Init_HLE()
{
    Wup028Adapter::Initialize();
    if (!PADInit()) {
        return 0;
    }
    KartPadKeyboard::ConfigureDefaultPort();
    return 1;
}
PPC_NATIVE_OVERRIDE(801AF2F0, PAD__Init_HLE, uint32_t, (), ());

// PADRead: gathers every GameCube pad source for the frame and writes the statuses to guest memory.
extern "C" uint32_t PAD__Read_HLE(uint32_t statusPtr)
{
    if (statusPtr == 0) {
        return 0;
    }

    PADStatus statuses[PAD_CHANMAX]{};
    // Keep looking for a Bluetooth Wii Remote that dropped out (or was turned on late).
    WiiRemoteInput::Poll();
    uint32_t rumbleMask = PADRead(statuses);
    RenewRumble();
    // Wii Remotes reach the game through KPAD, not as GameCube pads. This also
    // applies while input is blocked (overlay open) so the port does not flip
    // between "connected" and "no controller" every time the overlay toggles.
    WiiRemoteInput::HideRemotesFromPad(statuses, PAD_CHANMAX);

    FillTriggersHeldByButtons(statuses);
    InputBindings::Apply(statuses);
    ControllerShake::Apply(statuses);

    try {
        for (uint32_t i = 0; i < PAD_CHANMAX; ++i) {
            WritePadStatus(statusPtr + static_cast<uint32_t>(i * PadStatusContract::kGuestStatusSize),
                           statuses[i]);
        }
    } catch (const Memory::AccessViolation&) {
        return 0;
    }

    return rumbleMask;
}
PPC_NATIVE_OVERRIDE(801AF44C, PAD__Read_HLE, uint32_t, (uint32_t statusPtr), (statusPtr));

extern "C" uint32_t PAD__Reset_HLE(uint32_t mask)
{
    return PADReset(mask) ? 1u : 0u;
}
PPC_NATIVE_OVERRIDE(801AF0DC, PAD__Reset_HLE, uint32_t, (uint32_t mask), (mask));

extern "C" uint32_t PAD__Recalibrate_HLE(uint32_t mask)
{
    return PADRecalibrate(mask) ? 1u : 0u;
}
PPC_NATIVE_OVERRIDE(801AF1E4, PAD__Recalibrate_HLE, uint32_t, (uint32_t mask), (mask));

extern "C" void PAD__ControlMotor_HLE(int32_t chan, uint32_t command)
{
    if (command == PAD_MOTOR_RUMBLE && RumbleStrength(static_cast<uint32_t>(chan)) <= 0.0f) {
        command = PAD_MOTOR_STOP;
    }
    const bool on = command == PAD_MOTOR_RUMBLE;
    if (chan >= 0 && chan < PAD_CHANMAX) {
        const bool changed = g_motorOn[chan] != on;
        g_motorOn[chan] = on;
        if (SDL_Gamepad* pad = VariableRumblePad(static_cast<uint32_t>(chan))) {
            // The game repeats its commands; only a change (or a stop) needs a report.
            if (changed || !on) SendRumble(pad, static_cast<uint32_t>(chan), on);
            return;
        }
    }
    PADControlMotor(chan, command);
}
PPC_NATIVE_OVERRIDE_VOID(801AF908, PAD__ControlMotor_HLE, (int32_t chan, uint32_t command), (chan, command));
