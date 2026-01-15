#include "playback-control.h"

#include <vector>
#include <list>
#include <map>

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

// --- Data Structures ---

struct QueueEntry {
    int playlist_id;
    int entry_idx;

    bool operator==(const QueueEntry &other) const {
        return (playlist_id == other.playlist_id && entry_idx == other.entry_idx);
    }
};

// --- Global State ---

static bool stop_after_queue_active = true; // Enabled by default
static std::list<QueueEntry> global_queue;
static std::map<int, std::vector<int>> local_snapshots;

// We use these to track "Where did we just come from?"
static int last_playing_playlist = -1;
static int last_playing_index = -1;

// --- Forward Declarations ---

static void update_stop_rule();
static void sync_global_queue();
static void perform_jump();
static void jump_timer_cb(void * data);
static void toggle_stop_after_queue();
static void update_menu_text();

// --- Logic ---

static void sync_global_queue() {
    int n_playlists = Playlist::n_playlists();

    for (int i = 0; i < n_playlists; i++) {
        Playlist p = Playlist::by_index(i);
        int n_queued = p.n_queued();
        std::vector<int> current_local;

        for (int q = 0; q < n_queued; q++) {
            current_local.push_back(p.queue_get_entry(q));
        }

        std::vector<int> &last_local = local_snapshots[i];

        // 1. Detect Additions
        for (int song_idx : current_local) {
            bool found = false;
            for (int old_idx : last_local) {
                if (song_idx == old_idx) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                global_queue.push_back({i, song_idx});
            }
        }

        // 2. Detect Removals
        auto it = global_queue.begin();
        while (it != global_queue.end()) {
            if (it->playlist_id == i) {
                bool still_exists = false;
                for (int song_idx : current_local) {
                    if (song_idx == it->entry_idx) {
                        still_exists = true;
                        break;
                    }
                }
                if (!still_exists) {
                    it = global_queue.erase(it);
                } else {
                    ++it;
                }
            } else {
                ++it;
            }
        }

        last_local = current_local;
    }
}

static void update_stop_rule() {
    if (!stop_after_queue_active) {
        aud_set_bool(nullptr, "stop_after_current_song", false);
        return;
    }
    // Only stop if the GLOBAL queue is empty
    bool should_stop = global_queue.empty();
    aud_set_bool(nullptr, "stop_after_current_song", should_stop);
}

static void perform_jump() {
    if (global_queue.empty()) return;

    QueueEntry next = global_queue.front();

    Playlist p = Playlist::by_index(next.playlist_id);
    if (next.playlist_id >= Playlist::n_playlists()) return;

    p.activate();
    p.set_position(next.entry_idx);

    // Remove from native queue to avoid duplicates
    int n_queued = p.n_queued();
    for (int i = 0; i < n_queued; i++) {
        if (p.queue_get_entry(i) == next.entry_idx) {
            p.queue_remove(i);
            break;
        }
    }

    p.start_playback();

    // Reset the stop rule to ensure we don't stop immediately
    if (stop_after_queue_active) {
         aud_set_bool(nullptr, "stop_after_current_song", false);
    }
}

static void jump_timer_cb(void * data) {
    timer_remove(TimerRate::Hz30, jump_timer_cb, data);
    perform_jump();
}

// --- Hooks ---

static void on_playlist_update(void * = nullptr, void * = nullptr) {
    sync_global_queue();
    update_stop_rule();
}

static void on_playback_end(void * = nullptr, void * = nullptr) {
    // Song ended naturally. If we have a queue, jump to it.
    if (!global_queue.empty()) {
        timer_add(TimerRate::Hz30, jump_timer_cb, nullptr);
    }
}

// Intercept the "Next" button here
static void on_playback_ready(void * = nullptr, void * = nullptr) {
    int current_pl = Playlist::playing_playlist().index();
    int candidate_pos = Playlist::playing_playlist().get_position();

    if (global_queue.empty()) {
        last_playing_playlist = current_pl;
        last_playing_index = candidate_pos;
        return;
    }

    QueueEntry next_q = global_queue.front();

    // 1. Is the song about to play THE queued song? (We made it!)
    if (current_pl == next_q.playlist_id && candidate_pos == next_q.entry_idx) {
        global_queue.pop_front();

        // Clean native queue
        Playlist p = Playlist::by_index(current_pl);
        int n_queued = p.n_queued();
        for (int i=0; i<n_queued; i++) {
             if (p.queue_get_entry(i) == candidate_pos) { p.queue_remove(i); break; }
        }

        last_playing_playlist = current_pl;
        last_playing_index = candidate_pos;
        return;
    }

    // 2. Is this a "Linear Advance" (Next Button pressed)?
    bool is_linear_advance = false;

    if (current_pl == last_playing_playlist) {
        // Standard Next: Last + 1
        if (candidate_pos == last_playing_index + 1) is_linear_advance = true;

        // Wrap Around Next: Last was end, now 0
        Playlist p = Playlist::by_index(current_pl);
        if (last_playing_index == p.n_entries() - 1 && candidate_pos == 0) is_linear_advance = true;
    }

    // HIJACK LOGIC
    if (is_linear_advance) {
        // CRITICAL FIX: Stop the current "wrong" song immediately.
        // If we don't stop it, the engine locks in the stream and our timer fires too late.
        aud_drct_stop();

        // Schedule the jump to happen in the next tick
        timer_add(TimerRate::Hz30, jump_timer_cb, nullptr);
        return;
    }

    // 3. If it's a manual Double-Click (not linear), we do nothing and let it play.
    last_playing_playlist = current_pl;
    last_playing_index = candidate_pos;
}

// --- Menu Management ---

static void toggle_stop_after_queue() {
    stop_after_queue_active = !stop_after_queue_active;
    update_stop_rule();
    update_menu_text();
}

static void update_menu_text() {
    aud_plugin_menu_remove(AudMenuID::Main, toggle_stop_after_queue);

    const char * name = stop_after_queue_active ? _("Stop After Queue (ON)") : _("Stop After Queue (OFF)");
    const char * icon = stop_after_queue_active ? "process-stop" : "media-playback-start";

    aud_plugin_menu_add(AudMenuID::Main, toggle_stop_after_queue, name, icon);
}

// --- Plugin Entry Points ---

bool PlaybackControl::init() {
    global_queue.clear();
    local_snapshots.clear();
    sync_global_queue();

    hook_associate("playlist update", on_playlist_update, nullptr);
    hook_associate("playback end", on_playback_end, nullptr);
    hook_associate("playback ready", on_playback_ready, nullptr);

    update_menu_text();
    update_stop_rule();

    return true;
}

void PlaybackControl::cleanup() {
    hook_dissociate("playlist update", on_playlist_update);
    hook_dissociate("playback end", on_playback_end);
    hook_dissociate("playback ready", on_playback_ready);
    timer_remove(TimerRate::Hz30, jump_timer_cb, nullptr);

    aud_plugin_menu_remove(AudMenuID::Main, toggle_stop_after_queue);

    if (stop_after_queue_active) {
        aud_set_bool(nullptr, "stop_after_current_song", false);
    }

    stop_after_queue_active = false;
    global_queue.clear();
    local_snapshots.clear();
}