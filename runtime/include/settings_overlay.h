#pragma once

#include <aurora/aurora.h>
#include <aurora/event.h>

namespace settings_overlay {
// Apply persistent controller settings once Aurora has discovered host devices.
void InitializeRuntimeSettings() noexcept;
// Draw the F10 settings bar before each Aurora present.
void HandleEvents(const AuroraEvent* events) noexcept;
void Draw() noexcept;
bool StartupScreenVisible() noexcept;
uint64_t PresentedFrameCount() noexcept;
void NotifyStrapInputAccepted() noexcept;
void AdvancePresentedFrame() noexcept;
// Mac: while another app is in front, holds the game still until KartPad comes back (unless the
// setting is off, the game is still starting, or it is online). True when it waited.
bool WaitWhileBackgroundPaused() noexcept;
// Put host controllers back to a neutral state before the process ends.
void ReleaseControllers() noexcept;
} // namespace settings_overlay
