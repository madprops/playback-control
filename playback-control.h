#ifndef PLAYBACK_CONTROL_H
#define PLAYBACK_CONTROL_H

#include <libaudcore/plugin.h>

class PlaybackControl : public GeneralPlugin
{
public:
    static constexpr PluginInfo info = {
        "Playback Control",
        "playback-control",
        nullptr,
        nullptr
    };

    constexpr PlaybackControl() : GeneralPlugin(info, false) {}

    bool init() override;
    void cleanup() override;
};

#endif