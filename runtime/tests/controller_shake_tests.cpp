// Host check for Shake for Trick's detector.
// clang++ -std=c++20 -Iruntime/include runtime/tests/controller_shake_tests.cpp runtime/src/controller_shake_detector.cpp && ./a.out
#include "controller_shake.h"

#include <cmath>
#include <cstdio>
#include <limits>

using ControllerShake::Detector;

namespace {

constexpr float kG = 9.80665f;
constexpr double kFrame = 1.0 / 60.0;

// Feeds `frames` frames of one reading from `start`; returns how many were tricks.
int Feed(Detector& d, float x, float y, float z, double& t, int frames, const float* gyro = nullptr) {
    int tricks = 0;
    for (int i = 0; i < frames; ++i, t += kFrame) {
        const float accel[3] = {x, y, z};
        tricks += d.Sample(accel, gyro, t) ? 1 : 0;
    }
    return tricks;
}

} // namespace

int main() {
    int failures = 0;
    auto expect = [&](bool ok, const char* what) {
        if (!ok) { std::printf("FAIL: %s\n", what); ++failures; }
    };

    {
        Detector d;
        double t = 10.0;
        // Held flat, the face up: gravity on y.
        expect(Feed(d, 0, kG, 0, t, 60) == 0, "resting never tricks");
        expect(Feed(d, 1.6f * kG, kG, 0, t, 1) == 1, "a sharp shake tricks");
        expect(Feed(d, 1.6f * kG, kG, 0, t, 5) == 0, "a held shake tricks once");
        expect(Feed(d, 0, kG, 0, t, 30) == 0, "settling");
        expect(Feed(d, 0, kG, 1.5f * kG, t, 1) == 1, "another shake after settling");
        expect(Feed(d, 0, kG, 0, t, 3) == 0, "settles quickly");
        expect(Feed(d, 0, kG, 1.5f * kG, t, 1) == 0, "a shake within the cooldown is used up");
        expect(Feed(d, 0, kG, 0, t, 60) == 0, "settling after the cooldown");
        expect(Feed(d, -1.1f * kG, kG, 0, t, 1) == 0, "a bump under 1.35 g");
    }
    {
        Detector d;
        double t = 0.0;
        const float accel[3] = {3 * kG, 0, 0};
        expect(!d.Sample(accel, nullptr, t), "the first sample never tricks");
        t += kFrame;
        expect(Feed(d, 3 * kG, 0, 0, t, 1) == 0, "has to settle before the first trick");
        expect(Feed(d, 3 * kG, 0, 0, t, 60) == 0, "held still sideways, it settles");
        expect(Feed(d, 3 * kG, 2 * kG, 0, t, 1) == 1, "then tricks");
    }
    {
        Detector d;
        double t = 0.0;
        expect(Feed(d, 0, kG, 0, t, 30) == 0, "resting");
        t += 1.0;  // the game paused
        expect(Feed(d, 2 * kG, kG, 0, t, 1) == 0, "after a gap it starts over");
        expect(Feed(d, 0, kG, 0, t, 30) == 0, "settling after the gap");
        expect(Feed(d, 2 * kG, kG, 0, t, 1) == 1, "tricks after settling");
    }
    {
        // Turned a quarter turn about z in 0.15 s, the gyroscope reporting it: gravity, fixed in
        // the room, turns the other way, from y to x. No trick.
        Detector d;
        double t = 0.0;
        expect(Feed(d, 0, kG, 0, t, 30) == 0, "resting before the turn");
        const int frames = 9;
        const float rate = static_cast<float>((M_PI / 2) / (frames * kFrame));
        const float gyro[3] = {0, 0, rate};
        int tricks = 0;
        for (int i = 1; i <= frames; ++i, t += kFrame) {
            const double angle = (M_PI / 2) * i / frames;
            const float accel[3] = {static_cast<float>(kG * std::sin(angle)), static_cast<float>(kG * std::cos(angle)), 0};
            tricks += d.Sample(accel, gyro, t) ? 1 : 0;
        }
        expect(tricks == 0, "a fast turn with the gyroscope is not a shake");
        expect(d.LastMagnitude() < 0.1, "the gyroscope keeps gravity turned with the controller");
        expect(Feed(d, kG, 0, 0, t, 30) == 0, "resting after the turn");
    }
    {
        Detector d;
        double t = 0.0;
        expect(Feed(d, 0, kG, 0, t, 30) == 0, "resting");
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float bad[3] = {nan, kG, 0};
        expect(!d.Sample(bad, nullptr, t), "a non-finite sample is ignored");
        const float good[3] = {2 * kG, kG, 0};
        expect(!d.Sample(good, nullptr, std::numeric_limits<double>::infinity()), "a non-finite time is ignored");
        t += kFrame;
        expect(Feed(d, 2 * kG, kG, 0, t, 1) == 1, "still armed after bad samples");
    }

    if (failures) std::printf("%d failed\n", failures); else std::printf("controller_shake: all passed\n");
    return failures ? 1 : 0;
}
