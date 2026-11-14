#pragma once

// ScummVM on the esp-cpp emulators: what a cart needs to run one game. The
// engine runs in its own task between scummvm_start() and its end (the game
// quits, or scummvm_stop()).

#include <cstdint>
#include <span>
#include <string>
#include <utility>

/// Detect the game in `game_dir` and start it; false if it can't start
bool scummvm_start(const std::string &game_dir);
/// True until the engine has finished
bool scummvm_running();
/// Why the engine stopped (empty: it quit normally)
std::string scummvm_status();
/// Park the engine (its sound stops) for the cart's pause menu
void scummvm_pause();
void scummvm_resume();
/// Save / load a state to / from `path` while parked (the game decides
/// whether it can save now)
bool scummvm_save(const std::string &path, int slot);
bool scummvm_load(const std::string &path, int slot);
/// Quit the game and wait for the engine to finish
void scummvm_stop();
/// The game screen as RGB565 (in the board's frame_buffer1) for screenshots
std::span<uint8_t> scummvm_video_buffer();
std::pair<int, int> scummvm_video_size();

/// Allocation tracking (SCUMMVM_ESP32_TRACK_ALLOCS builds; no-ops otherwise):
/// a new game run, and a report of what the current run left allocated
void scummvm_alloc_new_generation();
void scummvm_alloc_report(const char *path);
