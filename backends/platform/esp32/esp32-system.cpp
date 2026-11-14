/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

// The ScummVM backend of the esp-cpp emulators (tab5-emu): an OSystem whose
// screen, sound and input are the board's (BoxEmu), and the functions its
// cart uses to run one game (include/scummvm_port.hpp). The engine runs in
// its own task; the cart parks it (in pollEvent() / delayMillis() /
// updateScreen()) for its pause menu, and saves / loads there.

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "common/scummsys.h"

#include "backends/events/default/default-events.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/fs/posix/posix-fs.h"
#include "backends/mixer/mixer.h"
#include "backends/modular-backend.h"
#include "backends/platform/esp32/esp32-graphics.h"
#include "backends/saves/default/default-saves.h"
#include "backends/timer/default/default-timer.h"
#include "audio/mixer_intern.h"
#include "base/plugins.h"
#include "common/config-manager.h"
#include "common/events.h"
#include "common/fs.h"
#include "common/keyboard.h"
#include "common/mutex.h"
#include "common/queue.h"
#include "common/savefile.h"
#include "engines/engine.h"
#include "engines/metaengine.h"

#include <atomic>
#include <csetjmp>
#include <cstdio>
#include <ctime>
#include <string>
#include <sys/time.h>

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "box-emu.hpp"
#include "scummvm_port.hpp"

namespace {

constexpr uint kSampleRate = 22050;
constexpr uint kMixSamples = 512; // per mixCallback(), stereo frames

class FreeRtosMutex : public Common::MutexInternal {
public:
	FreeRtosMutex() : _m(xSemaphoreCreateRecursiveMutex()) {}
	~FreeRtosMutex() override { vSemaphoreDelete(_m); }
	bool lock() override { return xSemaphoreTakeRecursive(_m, portMAX_DELAY) == pdTRUE; }
	bool unlock() override { return xSemaphoreGiveRecursive(_m) == pdTRUE; }

private:
	SemaphoreHandle_t _m;
};

// Saves go to the file the cart names for the slot (redirected for the
// duration of one save / load) and to the savepath otherwise (the engine's
// own lists).
class Esp32SaveFileManager : public DefaultSaveFileManager {
public:
	Common::String redirect;
	/// the file the next load reads (a game started to load a state)
	Common::String loadOnce;

	Common::InSaveFile *openForLoading(const Common::String &filename) override {
		if (!loadOnce.empty()) {
			Common::FSNode node{Common::Path(loadOnce, '/')};
			loadOnce.clear();
			return node.exists() ? node.createReadStream() : nullptr;
		}
		if (redirect.empty())
			return DefaultSaveFileManager::openForLoading(filename);
		Common::FSNode node{Common::Path(redirect, '/')};
		return node.exists() ? node.createReadStream() : nullptr;
	}
	Common::OutSaveFile *openForSaving(const Common::String &filename, bool compress) override {
		if (redirect.empty())
			return DefaultSaveFileManager::openForSaving(filename, compress);
		Common::FSNode node{Common::Path(redirect, '/')};
		Common::SeekableWriteStream *s = node.createWriteStream(false);
		return s ? new Common::OutSaveFile(s) : nullptr;
	}
	bool exists(const Common::String &filename) override {
		if (redirect.empty())
			return DefaultSaveFileManager::exists(filename);
		return Common::FSNode(Common::Path(redirect, '/')).exists();
	}
};

class Esp32MixerManager : public MixerManager {
public:
	void init() override {
		_mixer = new Audio::MixerImpl(kSampleRate, true, kMixSamples);
		_mixer->setReady(true);
	}
	void suspendAudio() override { _audioSuspended = true; }
	int resumeAudio() override {
		if (!_audioSuspended)
			return -2;
		_audioSuspended = false;
		return 0;
	}
	bool suspended() const { return _audioSuspended; }
	Audio::MixerImpl *mixer() { return _mixer; }
};

} // namespace

class OSystem_Esp32 : public ModularMixerBackend, public ModularGraphicsBackend, Common::EventSource {
public:
	OSystem_Esp32() { _fsFactory = new POSIXFilesystemFactory(); }

	void initBackend() override {
		_startUs = esp_timer_get_time();
		_timerManager = new DefaultTimerManager();
		_eventManager = new DefaultEventManager(this);
		_savefileManager = new Esp32SaveFileManager();
		_graphicsManager = new Esp32GraphicsManager();
		_mixerManager = new Esp32MixerManager();
		_mixerManager->init();
		BaseBackend::initBackend();
	}

	Esp32GraphicsManager *graphics() { return (Esp32GraphicsManager *)_graphicsManager; }
	Esp32MixerManager *mixerManager() { return (Esp32MixerManager *)_mixerManager; }
	Esp32SaveFileManager *saves() { return (Esp32SaveFileManager *)_savefileManager; }

	bool pollEvent(Common::Event &event) override;

	Common::MutexInternal *createMutex() override { return new FreeRtosMutex(); }
	uint32 getMillis(bool skipRecord = false) override { return (uint32)((esp_timer_get_time() - _startUs) / 1000); }
	void delayMillis(uint msecs) override;
	void getTimeAndDate(TimeDate &td, bool skipRecord = false) const override {
		time_t now = time(nullptr);
		struct tm t;
		localtime_r(&now, &t);
		td.tm_sec = t.tm_sec;
		td.tm_min = t.tm_min;
		td.tm_hour = t.tm_hour;
		td.tm_mday = t.tm_mday;
		td.tm_mon = t.tm_mon;
		td.tm_year = t.tm_year;
		td.tm_wday = t.tm_wday;
	}
	// Engines quit with this and expect the process to end (desktop
	// backends exit()): it jumps back to the engine task's start, which
	// deletes the engine
	void quit() override;
	void logMessage(LogMessageType::Type type, const char *message) override { fputs(message, stdout); }
	void addSysArchivesToSearchSet(Common::SearchSet &s, int priority) override {}
	Common::Path getDefaultConfigFileName() override { return Common::Path("/sdcard/saves/scummvm.ini", '/'); }

	/// The engine's safe points: timers, the cart's requests (pause / save / load)
	void service();

	// requests from the cart (another task), served on the engine task
	std::atomic<bool> pauseRequested{false};
	std::atomic<bool> quitRequested{false};
	std::atomic<bool> parked{false};
	enum class Request { NONE, SAVE };
	std::atomic<Request> request{Request::NONE};
	std::string requestPath;
	int requestSlot = 0;
	bool requestOk = false;
	SemaphoreHandle_t requestDone = xSemaphoreCreateBinary();
	bool _quit = false;

private:
	void pollInput();
	void pushMouse(Common::EventType type) {
		Common::Event e;
		e.type = type;
		e.mouse = Common::Point(graphics()->mouseX(), graphics()->mouseY());
		_events.push(e);
	}
	void pushKey(Common::EventType type, Common::KeyCode code, uint16 ascii, byte flags) {
		Common::Event e;
		e.type = type;
		e.kbd = Common::KeyState(code, ascii, flags);
		_events.push(e);
	}
	void moveMouseTo(int x, int y) {
		if (x == graphics()->mouseX() && y == graphics()->mouseY())
			return;
		graphics()->warpMouse(x, y);
		pushMouse(Common::EVENT_MOUSEMOVE);
	}

	int64_t _startUs = 0;
	Common::Queue<Common::Event> _events;
	uint32 _lastInputMs = 0;
	// input state (to generate press / release events)
	int _touchFingers = 0;
	bool _touchRight = false;
	uint16_t _pad = 0;
	uint8_t _mouseButtons = 0;
	uint8_t _keys[32] = {};
	float _padMouseX = 0, _padMouseY = 0, _padSpeed = 0;
};

static OSystem_Esp32 *s_system = nullptr;

static jmp_buf s_quitJump;
static TaskHandle_t s_quitJumpTask = nullptr;

void OSystem_Esp32::quit() {
	_quit = true;
	if (s_quitJumpTask && xTaskGetCurrentTaskHandle() == s_quitJumpTask)
		longjmp(s_quitJump, 1);
}

void OSystem_Esp32::delayMillis(uint msecs) {
	service();
	graphics()->updateScreen(); // a frame that was rate limited away
	vTaskDelay(pdMS_TO_TICKS(MAX<uint>(msecs, 1)));
}

bool OSystem_Esp32::pollEvent(Common::Event &event) {
	service();
	pollInput();
	if (_events.empty())
		return false;
	event = _events.pop();
	return true;
}

void OSystem_Esp32::service() {
	((DefaultTimerManager *)getTimerManager())->checkTimers();
	// (an intro may only delay, never poll: quit from here as well)
	if (quitRequested.exchange(false))
		quit();
	if (!pauseRequested)
		return;
	// park (the cart's pause menu is up): the engine is paused, its sound
	// stops; saves and loads requested meanwhile are done here
	{
		PauseToken token = g_engine ? g_engine->pauseEngine() : PauseToken();
		parked = true;
		while (pauseRequested && !quitRequested) {
			const Request r = request.load();
			if (r != Request::NONE) {
				saves()->redirect = requestPath.c_str();
				if (!g_engine) {
					requestOk = false;
				} else {
					// (the game's own rules: not during a cutscene, etc.)
					requestOk = g_engine->canSaveGameStateCurrently() &&
					            g_engine->saveGameState(requestSlot, Common::String::format("Slot %d", requestSlot)).getCode() == Common::kNoError;
				}
				saves()->redirect.clear();
				request = Request::NONE;
				xSemaphoreGive(requestDone);
			}
			vTaskDelay(pdMS_TO_TICKS(10));
		}
		parked = false;
	}
	if (quitRequested.exchange(false))
		quit();
	// the menu drew over the frame and set its own palette
	graphics()->refresh();
	// input made in the menu doesn't reach the game
	_touchFingers = BoxEmu::get().touchpad_data().num_touch_points;
	_pad = 0xFFFF;
}

// HID usage -> key code (US layout)
static Common::KeyCode hidToKey(int usage, uint16 &ascii, bool shift) {
	ascii = 0;
	if (usage >= 0x04 && usage <= 0x1D) {
		const char c = 'a' + (usage - 0x04);
		ascii = shift ? c - 32 : c;
		return (Common::KeyCode)(Common::KEYCODE_a + (usage - 0x04));
	}
	if (usage >= 0x1E && usage <= 0x27) {
		const int d = usage == 0x27 ? 0 : usage - 0x1E + 1;
		ascii = '0' + d;
		return (Common::KeyCode)(Common::KEYCODE_0 + d);
	}
	if (usage >= 0x3A && usage <= 0x45)
		return (Common::KeyCode)(Common::KEYCODE_F1 + (usage - 0x3A));
	switch (usage) {
	case 0x28: ascii = 13; return Common::KEYCODE_RETURN;
	case 0x29: ascii = 27; return Common::KEYCODE_ESCAPE;
	case 0x2A: ascii = 8; return Common::KEYCODE_BACKSPACE;
	case 0x2B: ascii = 9; return Common::KEYCODE_TAB;
	case 0x2C: ascii = ' '; return Common::KEYCODE_SPACE;
	case 0x2D: ascii = '-'; return Common::KEYCODE_MINUS;
	case 0x2E: ascii = '='; return Common::KEYCODE_EQUALS;
	case 0x33: ascii = ';'; return Common::KEYCODE_SEMICOLON;
	case 0x36: ascii = ','; return Common::KEYCODE_COMMA;
	case 0x37: ascii = '.'; return Common::KEYCODE_PERIOD;
	case 0x38: ascii = '/'; return Common::KEYCODE_SLASH;
	case 0x4F: return Common::KEYCODE_RIGHT;
	case 0x50: return Common::KEYCODE_LEFT;
	case 0x51: return Common::KEYCODE_DOWN;
	case 0x52: return Common::KEYCODE_UP;
	default: return Common::KEYCODE_INVALID;
	}
}

void OSystem_Esp32::pollInput() {
	const uint32 now = getMillis();
	if (now - _lastInputMs < 10)
		return;
	const uint32 dtMs = now - _lastInputMs;
	_lastInputMs = now;
	auto &emu = BoxEmu::get();
	Esp32GraphicsManager *gfx = graphics();
	const int w = gfx->activeWidth(), h = gfx->activeHeight();

	// touch: the screen is a pointer (tap = left click, two fingers = right
	// click); the frame is centered in the display at display_size()
	const auto touch = emu.touchpad_data();
	const int fingers = touch.num_touch_points;
	if (fingers > 0 && w > 0 && h > 0) {
		const auto [dw, dh] = emu.display_size();
		const int ox = ((int)emu.lcd_width() - (int)dw) / 2, oy = ((int)emu.lcd_height() - (int)dh) / 2;
		const int x = CLIP<int>(((int)touch.x - ox) * w / (int)MAX<size_t>(1, dw), 0, w - 1);
		const int y = CLIP<int>(((int)touch.y - oy) * h / (int)MAX<size_t>(1, dh), 0, h - 1);
		if (_touchFingers == 0)
			moveMouseTo(x, y);
		else if (!_touchRight)
			moveMouseTo(x, y); // drag
		if (_touchFingers == 0) {
			pushMouse(Common::EVENT_LBUTTONDOWN);
			_touchRight = false;
		} else if (fingers >= 2 && !_touchRight) {
			// a second finger turns the press into a right click
			pushMouse(Common::EVENT_LBUTTONUP);
			pushMouse(Common::EVENT_RBUTTONDOWN);
			_touchRight = true;
		}
	} else if (_touchFingers > 0) {
		pushMouse(_touchRight ? Common::EVENT_RBUTTONUP : Common::EVENT_LBUTTONUP);
		_touchRight = false;
	}
	_touchFingers = fingers;

	// USB mouse: relative motion, left / right buttons
	if (emu.usb_mouse_present()) {
		const auto m = emu.take_mouse_motion();
		if (m.dx || m.dy)
			moveMouseTo(CLIP(gfx->mouseX() + m.dx / 2, 0, w - 1), CLIP(gfx->mouseY() + m.dy / 2, 0, h - 1));
		const uint8_t changed = m.buttons ^ _mouseButtons;
		if (changed & 1)
			pushMouse(m.buttons & 1 ? Common::EVENT_LBUTTONDOWN : Common::EVENT_LBUTTONUP);
		if (changed & 2)
			pushMouse(m.buttons & 2 ? Common::EVENT_RBUTTONDOWN : Common::EVENT_RBUTTONUP);
		_mouseButtons = m.buttons;
	}

	// gamepad (USB pad, the keyboard's pad keys and the link's test control,
	// without the touch zones): the d-pad / stick moves the pointer, A = left
	// click, B = right click, START = Escape, SELECT = Space
	GamepadState pad = emu.gamepad_state();
	pad.buttons &= ~emu.touch_gamepad_state().buttons;
	if (_pad == 0xFFFF)
		_pad = pad.buttons; // just resumed: held buttons don't count
	const auto axes = emu.gamepad_axes();
	float vx = (pad.right ? 1.f : 0.f) - (pad.left ? 1.f : 0.f) + axes.lx / 32768.f;
	float vy = (pad.down ? 1.f : 0.f) - (pad.up ? 1.f : 0.f) + axes.ly / 32768.f;
	if (ABS(vx) < 0.15f)
		vx = 0;
	if (ABS(vy) < 0.15f)
		vy = 0;
	if (vx != 0 || vy != 0) {
		// accelerates from ~60 to ~240 px/s
		_padSpeed = MIN(_padSpeed + dtMs * 0.0004f, 0.24f);
		const float step = MAX(_padSpeed, 0.06f) * dtMs;
		_padMouseX = CLIP<float>(_padMouseX + vx * step, 0, w - 1);
		_padMouseY = CLIP<float>(_padMouseY + vy * step, 0, h - 1);
		moveMouseTo((int)_padMouseX, (int)_padMouseY);
	} else {
		_padSpeed = 0;
		_padMouseX = gfx->mouseX();
		_padMouseY = gfx->mouseY();
	}
	const uint16_t pressed = pad.buttons & ~_pad, released = _pad & ~pad.buttons;
	GamepadState p, r;
	p.buttons = pressed;
	r.buttons = released;
	if (p.a) pushMouse(Common::EVENT_LBUTTONDOWN);
	if (r.a) pushMouse(Common::EVENT_LBUTTONUP);
	if (p.b) pushMouse(Common::EVENT_RBUTTONDOWN);
	if (r.b) pushMouse(Common::EVENT_RBUTTONUP);
	if (p.start) pushKey(Common::EVENT_KEYDOWN, Common::KEYCODE_ESCAPE, 27, 0);
	if (r.start) pushKey(Common::EVENT_KEYUP, Common::KEYCODE_ESCAPE, 27, 0);
	if (p.select) pushKey(Common::EVENT_KEYDOWN, Common::KEYCODE_SPACE, ' ', 0);
	if (r.select) pushKey(Common::EVENT_KEYUP, Common::KEYCODE_SPACE, ' ', 0);
	_pad = pad.buttons;

	// USB keyboard
	if (emu.usb_keyboard_present()) {
		const auto kb = emu.keyboard_state();
		auto down = [](const uint8_t *k, int u) { return (k[u >> 3] >> (u & 7)) & 1; };
		const bool shift = down(kb.keys, 0xE1) || down(kb.keys, 0xE5);
		byte flags = 0;
		if (shift)
			flags |= Common::KBD_SHIFT;
		if (down(kb.keys, 0xE0) || down(kb.keys, 0xE4))
			flags |= Common::KBD_CTRL;
		if (down(kb.keys, 0xE2) || down(kb.keys, 0xE6))
			flags |= Common::KBD_ALT;
		for (int u = 0x04; u < 0xE0; u++) {
			const int now_ = down(kb.keys, u), was = down(_keys, u);
			if (now_ == was)
				continue;
			uint16 ascii;
			const Common::KeyCode code = hidToKey(u, ascii, shift);
			if (code != Common::KEYCODE_INVALID)
				pushKey(now_ ? Common::EVENT_KEYDOWN : Common::EVENT_KEYUP, code, ascii, flags);
		}
		memcpy(_keys, kb.keys, sizeof(_keys));
	}
}

/////////////////////////////////////////////////////////////////////////////
// The cart's side
/////////////////////////////////////////////////////////////////////////////

namespace {

TaskHandle_t s_engineTask = nullptr;
TaskHandle_t s_audioTask = nullptr;
std::atomic<bool> s_running{false};
std::atomic<bool> s_audioStop{false};
std::atomic<bool> s_audioDone{false};
std::string s_gameDir;
std::string s_status;
// a state to load when the game starts (save_slot)
std::string s_loadPath;
int s_loadSlot = -1;

void audioTask(void *) {
	const size_t bytes = kMixSamples * 4;
	byte *buf = (byte *)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	while (!s_audioStop) {
		Esp32MixerManager *mm = s_system->mixerManager();
		if (s_system->parked || mm->suspended()) {
			vTaskDelay(pdMS_TO_TICKS(10));
			continue;
		}
		mm->mixer()->mixCallback(buf, bytes);
		// paced: waits for room in the board's audio queue
		BoxEmu::get().play_audio(buf, bytes);
	}
	heap_caps_free(buf);
	// (tasks made WithCaps are freed by vTaskDeleteWithCaps(), from
	// scummvm_stop(): a self-delete would leave the stack behind)
	s_audioDone = true;
	vTaskSuspend(nullptr);
}

// the running engine (for the cleanup after OSystem::quit() jumped out)
struct RunningEngine {
	Engine *engine = nullptr;
	MetaEngine *metaEngine = nullptr;
	DetectedGame game;
	const void *descriptor = nullptr;
	Common::String target;
} s_run;

void endEngine() {
	if (s_run.engine) {
		g_system->engineDone();
		s_run.metaEngine->deleteInstance(s_run.engine, s_run.game, s_run.descriptor);
	}
	s_run.engine = nullptr;
	s_run.metaEngine = nullptr;
	s_run.game = DetectedGame();
	SearchMan.clear();
	if (!s_run.target.empty())
		ConfMan.removeGameDomain(s_run.target);
	s_run.target.clear();
	g_system->getMixer()->stopAll();
}

// Detect the game and make its engine (into s_run). Nothing of this stays on
// the stack while the engine runs: OSystem::quit() jumps over those frames.
Common::Error createEngine() {
	Common::FSNode dir{Common::Path(s_gameDir.c_str(), '/')};
	Common::FSList files;
	if (!dir.getChildren(files, Common::FSNode::kListAll))
		return Common::Error(Common::kPathDoesNotExist, s_gameDir.c_str());
	DetectionResults results = EngineMan.detectGames(files);
	DetectedGames games = results.listRecognizedGames();
	if (games.empty())
		return Common::Error(Common::kNoGameDataFoundError, s_gameDir.c_str());
	const DetectedGame &detected = games[0];
	printf("[scummvm] %s: %s (%s)\n", s_gameDir.c_str(), detected.description.c_str(), detected.gameId.c_str());

	const Common::String target = EngineMan.createTargetForGame(detected);
	s_run.target = target;
	ConfMan.setActiveDomain(target);
	ConfMan.set("music_driver", "adlib", target);
	if (s_loadSlot >= 0) {
		// the engine loads the state as it starts (from the cart's slot file)
		ConfMan.setInt("save_slot", s_loadSlot, target);
		s_system->saves()->loadOnce = s_loadPath.c_str();
		s_loadSlot = -1;
	}

	const Plugin *detectionPlugin = EngineMan.findDetectionPlugin(detected.engineId);
	if (!detectionPlugin)
		return Common::kMetaEnginePluginNotFound;
	DetectedGame game;
	const void *descriptor = nullptr;
	Common::Error err = detectionPlugin->get<MetaEngineDetection>().identifyGame(game, &descriptor);
	if (err.getCode() != Common::kNoError)
		return err;
	const Plugin *enginePlugin = PluginMan.findEnginePlugin(detected.engineId);
	if (!enginePlugin)
		return Common::kEnginePluginNotFound;
	MetaEngine &metaEngine = enginePlugin->get<MetaEngine>();
	metaEngine.registerDefaultSettings(target);
	Engine *engine = nullptr;
	err = metaEngine.createInstance(g_system, &engine, game, descriptor);
	if (err.getCode() != Common::kNoError || !engine) {
		metaEngine.deleteInstance(engine, game, descriptor);
		return err.getCode() != Common::kNoError ? err : Common::Error(Common::kUnknownError);
	}
	engine->setMetaEngine(&metaEngine);
	engine->initializePath(dir);
	s_run.engine = engine;
	s_run.metaEngine = &metaEngine;
	s_run.game = game;
	s_run.descriptor = descriptor;
	return Common::kNoError;
}

Common::Error runEngine() {
	const Common::Error err = createEngine();
	if (err.getCode() != Common::kNoError)
		return err;
	g_system->engineInit();
	g_system->getEventManager()->purgeKeyboardEvents();
	g_system->getEventManager()->purgeMouseEvents();
	return s_run.engine->run();
}

void engineTask(void *) {
	Common::Error err = Common::kNoError;
	s_quitJumpTask = xTaskGetCurrentTaskHandle();
	if (setjmp(s_quitJump) == 0)
		err = runEngine();
	s_quitJumpTask = nullptr;
	endEngine();
	scummvm_alloc_report("/sdcard/saves/scummvm_leaks.txt");
	s_status = err.getCode() == Common::kNoError ? "" : err.getDesc().c_str();
	printf("[scummvm] engine done: %s\n", s_status.empty() ? "ok" : s_status.c_str());
	s_running = false;
	vTaskSuspend(nullptr); // deleted by scummvm_stop()
}

} // namespace

static bool start(const std::string &game_dir, bool paused) {
	if (s_running)
		return false;
	if (!s_system) {
		s_system = new OSystem_Esp32();
		g_system = s_system;
		s_system->initBackend();
		PluginManager::instance().init();
		PluginManager::instance().loadAllPlugins();
		PluginManager::instance().loadDetectionPlugin();
		ConfMan.loadDefaultConfigFile(Common::Path());
		ConfMan.set("savepath", "/sdcard/saves", Common::ConfigManager::kApplicationDomain);
	}
	s_gameDir = game_dir;
	s_status.clear();
	scummvm_alloc_new_generation();
	s_system->_quit = false;
	s_system->quitRequested = false;
	s_system->pauseRequested = paused;
	s_system->getMixer()->stopAll();
	auto &emu = BoxEmu::get();
	emu.audio_sample_rate(kSampleRate);
	emu.audio_max_wait_ms(30);
	s_audioStop = false;
	s_audioDone = false;
	s_running = true;
	xTaskCreatePinnedToCoreWithCaps(audioTask, "svm_audio", 6 * 1024, nullptr, 15, &s_audioTask, 1,
	                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	// engines recurse deeply: a big stack, in PSRAM
	if (xTaskCreatePinnedToCoreWithCaps(engineTask, "scummvm", 96 * 1024, nullptr, 5, &s_engineTask, 1,
	                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
		s_running = false;
		return false;
	}
	return true;
}

bool scummvm_start(const std::string &game_dir) { return start(game_dir, false); }

bool scummvm_running() { return s_running; }

std::string scummvm_status() { return s_status; }

void scummvm_pause() {
	if (!s_running || !s_system)
		return;
	s_system->pauseRequested = true;
	for (int i = 0; i < 500 && s_running && !s_system->parked; i++)
		vTaskDelay(pdMS_TO_TICKS(10));
	// (tracking builds: what the game holds now)
	scummvm_alloc_report("/sdcard/saves/scummvm_live.txt");
}

void scummvm_resume() {
	if (!s_system)
		return;
	s_system->pauseRequested = false;
}

static bool request(OSystem_Esp32::Request r, const std::string &path, int slot) {
	if (!s_running || !s_system || !s_system->parked)
		return false;
	s_system->requestPath = path;
	s_system->requestSlot = slot;
	xSemaphoreTake(s_system->requestDone, 0);
	s_system->request = r;
	if (xSemaphoreTake(s_system->requestDone, pdMS_TO_TICKS(20000)) != pdTRUE)
		return false;
	return s_system->requestOk;
}

bool scummvm_save(const std::string &path, int slot) { return request(OSystem_Esp32::Request::SAVE, path, slot); }

// Engines load a state best as they start (ScummVM's save_slot): the game
// is restarted with it, parked like the one it replaces (the pause menu is up)
bool scummvm_load(const std::string &path, int slot) {
	if (!s_running || !s_system || path.empty() || !Common::FSNode(Common::Path(path.c_str(), '/')).exists())
		return false;
	const std::string dir = s_gameDir;
	scummvm_stop();
	s_loadPath = path;
	s_loadSlot = slot;
	return start(dir, true);
}

void scummvm_stop() {
	if (s_running) {
		s_system->quitRequested = true;
		scummvm_resume();
		// (the board's buffers go when the cart does: the engine must be gone)
		for (int i = 0; s_running; i++) {
			if (i == 500)
				printf("[scummvm] still waiting for the engine to quit\n");
			vTaskDelay(pdMS_TO_TICKS(10));
		}
	}
	// the engine task has suspended itself (or never started)
	if (s_engineTask) {
		while (eTaskGetState(s_engineTask) != eSuspended)
			vTaskDelay(pdMS_TO_TICKS(1));
		vTaskDeleteWithCaps(s_engineTask);
		s_engineTask = nullptr;
	}
	if (s_audioTask) {
		s_audioStop = true;
		while (!s_audioDone || eTaskGetState(s_audioTask) != eSuspended)
			vTaskDelay(pdMS_TO_TICKS(1));
		vTaskDeleteWithCaps(s_audioTask);
		s_audioTask = nullptr;
	}
	BoxEmu::get().audio_sample_rate(48000);
}

std::pair<int, int> scummvm_video_size() {
	if (!s_system)
		return {320, 200};
	return {s_system->graphics()->getWidth(), s_system->graphics()->getHeight()};
}

std::span<uint8_t> scummvm_video_buffer() {
	if (!s_system)
		return {};
	int w = 0, h = 0;
	uint16_t *dst = (uint16_t *)BoxEmu::get().frame_buffer1();
	const size_t n = s_system->graphics()->snapshotRgb565(dst, BoxEmu::FRAME_BUFFER_PIXELS, w, h);
	return std::span<uint8_t>((uint8_t *)dst, n * 2);
}
