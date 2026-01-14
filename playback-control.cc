#include "playback-control.h"
#include <libaudcore/i18n.h>
#include <libaudcore/runtime.h>
#include <libaudcore/drct.h>
#include <libaudcore/playlist.h>
#include <libaudcore/hook.h>
#include <libaudcore/interface.h>
#include <libaudcore/audstrings.h>
#include <libaudcore/plugin.h>
#include <libaudcore/preferences.h>

EXPORT PlaybackControl aud_plugin_instance;

static bool is_active = false;

// Forward Declarations
static void update_stop_rule(void * = nullptr, void * = nullptr);
static void update_menu_items();
static void toggle_stop_after_queue();

// --- Logic ---

static void update_stop_rule(void * /* data */, void * /* user */)
{
    if (!is_active) return;

    Playlist playlist = Playlist::active_playlist();

    // If Queue is empty (0), we set "stop_after_current_song" to TRUE.
    // This causes the Stop button in the UI to light up/highlight.
    bool should_stop = (playlist.n_queued() == 0);

    aud_set_bool(nullptr, "stop_after_current_song", should_stop);
}

// --- Menu Management ---

static constexpr AudMenuID menus[] = {
    AudMenuID::Main,
    AudMenuID::Playlist
};

// We use this to refresh the menu text
static void update_menu_items()
{
    // 1. Remove the existing items (using the function pointer to identify them)
    for (AudMenuID menu : menus)
    {
        aud_plugin_menu_remove(menu, toggle_stop_after_queue);
    }

    // 2. Define the new name based on state
    const char * name = is_active ? _("Stop After Queue (ON)") : _("Stop After Queue (OFF)");
    const char * icon = is_active ? "process-stop" : "media-playback-start";

    // 3. Re-add the items
    for (AudMenuID menu : menus)
    {
        aud_plugin_menu_add(menu, toggle_stop_after_queue, name, icon);
    }
}

static void toggle_stop_after_queue()
{
    is_active = !is_active;

    if (is_active)
    {
        // Enabled
        update_stop_rule(); // Check immediately
        hook_associate("playback ready", update_stop_rule, nullptr);
        hook_associate("playlist update", update_stop_rule, nullptr);
        AUDINFO("Stop After Queue: ENABLED\n");
    }
    else
    {
        // Disabled
        hook_dissociate("playback ready", update_stop_rule);
        hook_dissociate("playlist update", update_stop_rule);

        // Reset the native stop button
        aud_set_bool(nullptr, "stop_after_current_song", false);
        AUDINFO("Stop After Queue: DISABLED\n");
    }

    // Update the visual menu text
    update_menu_items();
}

// --- Plugin Entry Points ---

bool PlaybackControl::init()
{
    // Add the initial menu items (defaults to OFF)
    update_menu_items();
    return true;
}

void PlaybackControl::cleanup()
{
    if (is_active)
    {
        hook_dissociate("playback ready", update_stop_rule);
        hook_dissociate("playlist update", update_stop_rule);
        aud_set_bool(nullptr, "stop_after_current_song", false);
    }

    // Remove menus
    for (AudMenuID menu : menus)
    {
        aud_plugin_menu_remove(menu, toggle_stop_after_queue);
    }

    is_active = false;
}