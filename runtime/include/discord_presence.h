#pragma once

#include <cstdint>
#include <string>

// Host implementation of Dolphin's /dev/dolphin Discord contract. The guest
// sends only strings and big-endian integer fields; the IPC wire protocol is
// owned here so translated game code never needs host SDK headers. Every call
// returns at once: a worker thread owns the connection to Discord.
namespace DiscordPresence {

struct Activity {
    std::string details;
    std::string state;
    std::string largeImageKey;
    std::string largeImageText;
    std::string smallImageKey;
    std::string smallImageText;
    int64_t startTimestamp = 0;
    int64_t endTimestamp = 0;
    uint32_t partySize = 0;
    uint32_t partyMax = 0;
};

void Initialize(const std::string& basicClientId, const std::string& basicTitle, bool enabled);
// Off closes the connection, which clears the status in Discord. What the game
// and host send meanwhile is kept, so turning it back on shows the latest.
void SetEnabled(bool enabled);
void SetClient(const std::string& clientId);
void SetActivity(Activity activity);
// The host's own lines under the basic client ID, keeping its start time. A
// game that sets its own client (Retro Rewind does) is shown instead.
void SetBasicActivity(const std::string& details, const std::string& state);
void Reset();
void Shutdown();

} // namespace DiscordPresence
