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

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef STARTREK_H
#define STARTREK_H

#include "common/events.h"
#include "common/hash-str.h"
#include "common/hashmap.h"
#include "common/list.h"
#include "common/ptr.h"
#include "common/random.h"
#include "common/rect.h"
#include "common/scummsys.h"
#include "common/serializer.h"
#include "common/str.h"
#include "common/stream.h"
#include "common/system.h"
#include "common/util.h"

#include "engines/engine.h"

#include "math/cosinetables.h"
#include "math/sinetables.h"

#include "startrek/action.h"
#include "startrek/awaymission.h"
#include "startrek/graphics.h"
#include "startrek/items.h"
#include "startrek/object.h"
#include "startrek/sound.h"
#include "startrek/space.h"
#include "startrek/detection.h"


using Common::SharedPtr;
using Common::String;

namespace Common {
class MacResManager;
}

namespace StarTrek {

class StarTrekEngine;
class Room;
class Console;
class Resource;

typedef String(StarTrekEngine::*TextGetterFunc)(int, uintptr, String *);

const int SAVEGAME_DESCRIPTION_LEN = 30;

struct SavegameMetadata {
	uint32 version;
	char description[SAVEGAME_DESCRIPTION_LEN + 1];

	uint32 saveDate;
	uint16 saveTime;
	byte saveTimeSecs;
	uint32 playTime;

	::Graphics::Surface *thumbnail;

	void setSaveTimeAndDate(TimeDate time) {
		saveDate = ((time.tm_mday & 0xFF) << 24) | (((time.tm_mon + 1) & 0xFF) << 16) | ((time.tm_year + 1900) & 0xFFFF);
		saveTime = ((time.tm_hour & 0xFF) << 8) | ((time.tm_min) & 0xFF);
		saveTimeSecs = time.tm_sec & 0xFF;
	}

	int getDay() {
		return (saveDate >> 24) & 0xFF;
	}
	int getMonth() {
		return (saveDate >> 16) & 0xFF;
	}
	int getYear() {
		return saveDate & 0xFFFF;
	}
	int getHour() {
		return (saveTime >> 8) & 0xFF;
	}
	int getMinute() {
		return saveTime & 0xFF;
	}
};


const int MAX_MENUBUTTONS = 32;
const int TEXTBOX_WIDTH = 26;
const int TEXT_CHARS_PER_LINE = TEXTBOX_WIDTH - 2;
const int MAX_TEXTBOX_LINES = 12;

const int TEXT_INPUT_BUFFER_SIZE = 134;
const int MAX_TEXT_INPUT_LEN = 20;

const int MAX_BUFFERED_WALK_ACTIONS = 32;

const int MAX_BAN_FILES = 16;




enum kDebugLevels {
	kDebugSound = 1,
	kDebugGraphics,
	kDebugSavegame,
	kDebugSpace,
	kDebugGeneral,
};

enum GameMode {
	GAMEMODE_START = 0,
	GAMEMODE_BRIDGE,
	GAMEMODE_AWAYMISSION,
	GAMEMODE_BEAMDOWN,
	GAMEMODE_BEAMUP
};

enum TextDisplayMode {
	TEXTDISPLAY_WAIT = 0,  // Wait for input before closing text
	TEXTDISPLAY_SUBTITLES, // Automatically continue when speech is done
	TEXTDISPLAY_NONE       // No text displayed
};

enum TextColor {
	TEXTCOLOR_GREY   = 0x88,
	TEXTCOLOR_RED    = 0xa1,
	TEXTCOLOR_YELLOW = 0xb0,
	TEXTCOLOR_BLUE   = 0xc0
};

// Keeps track of data for a list of buttons making up a menu
struct Menu {
	Sprite sprites[MAX_MENUBUTTONS];
	uint16 retvals[MAX_MENUBUTTONS];
	uint32 disabledButtons;
	uint16 numButtons;
	int16 selectedButton;
	Menu *nextMenu;
};

// Special events that can be returned by handleMenuEvents.
// (Normally it returns the "retval" of a pressed button, which is positive.)
enum MenuEvent {
	MENUEVENT_RCLICK_OFFBUTTON = -4,
	MENUEVENT_ENABLEINPUT,          // Makes buttons selectable (occurs after a delay)
	MENUEVENT_RCLICK_ONBUTTON,
	MENUEVENT_LCLICK_OFFBUTTON
};

// Buttons for standard text display
enum TextButtons {
	TEXTBUTTON_CONFIRM = 0,
	TEXTBUTTON_SCROLLUP,
	TEXTBUTTON_SCROLLDOWN,
	TEXTBUTTON_PREVCHOICE,
	TEXTBUTTON_NEXTCHOICE,
	TEXTBUTTON_SCROLLUP_ONELINE,
	TEXTBUTTON_SCROLLDOWN_ONELINE,
	TEXTBUTTON_GOTO_TOP,
	TEXTBUTTON_GOTO_BOTTOM,
	TEXTBUTTON_SPEECH_DONE // "Virtual" button?
};

// Buttons for option menu (corresponding to button indices, not button retvals, which are
// different for some reason)
enum OptionMenuButtons {
	OPTIONBUTTON_TEXT,
	OPTIONBUTTON_SAVE,
	OPTIONBUTTON_LOAD,
	OPTIONBUTTON_ENABLEMUSIC,
	OPTIONBUTTON_DISABLEMUSIC,
	OPTIONBUTTON_ENABLESFX,
	OPTIONBUTTON_DISABLESFX,
	OPTIONBUTTON_QUIT
};

enum TrekEventType {
	TREKEVENT_TICK = 0, // DOS clock changes
	TREKEVENT_LBUTTONDOWN = 1,
	TREKEVENT_MOUSEMOVE = 2,
	TREKEVENT_LBUTTONUP = 3,
	TREKEVENT_RBUTTONDOWN = 4,
	TREKEVENT_RBUTTONUP = 5,
	TREKEVENT_KEYDOWN = 6
};

struct TrekEvent {
	TrekEventType type;
	Common::KeyState kbd;
	Common::Point mouse;
	uint32 tick;
};

// An action of the bridge's queue (bridge.h, BridgeActionType)
struct BridgeAction {
	int16 type;
	int16 data;
};

struct ComputerTopic {
	Common::String fileName;
	Common::String topic;
};

struct EnterpriseState {
	bool shields;
	bool weapons;
	bool underAttack;
	bool inOrbit;
	bool targetAnalysis;

	EnterpriseState() {
		shields = false;
		weapons = false;
		underAttack = false;
		inOrbit = false;
		targetAnalysis = false;
	}
};

class Graphics;
class IWFile;
class Sound;

class StarTrekEngine : public ::Engine {
protected:
	// startrek.cpp
public:
	StarTrekEngine(OSystem *syst, const StarTrekGameDescription *gamedesc);
	~StarTrekEngine() override;

	friend class Console;

	Common::Error run() override;

	// Saving through ScummVM's API (e.g. a backend's own menu), while the
	// game waits for input in the bridge / away mission loop. (Loading is
	// at startup: the save_slot setting.)
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	Common::Error saveGameState(int slot, const Common::String &desc, bool isAutosave = false) override;
	Common::Error runGameMode(int mode, bool resume);

	// Transporter room
	void runTransportSequence(const Common::String &name);

	// Bridge
	void initBridge(bool b);
	void loadBridge();
	void loadBridgeActors();
	void cleanupBridge();
	void runBridge();
	void setBridgeMouseCursor();
	void handleBridgeEvents();
	void handleBridgeComputer();

	// bridgemissions.cpp: the missions' handlers and the bridge's actions
	void postBridgeAction(int type, int data = 0);
	void clearBridgeActions();
	void swallowBridgeAction();
	void setBridgeHandler(int handler);
	void runBridgeActions();
	void runBridgeHandler();
	void bridgeDefaultAction();
	void bridgeTick();
	void bridgeText(int talker, int textId, int flag = 0);
	void bridgeText(int talker, const Common::String &text, int flag = 0);
	void viewscreenTalker(const Common::String &anim);
	void viewscreenTalkerText(const Common::String &name, int textId);
	void viewscreenTalkerText(const Common::String &name, const Common::String &text) {
		showTextbox(name, text, 160, 190, 44, 0);
	}
	void restartViewscreenTalker();
	void removeViewscreenTalker();
	void viewscreenMessage(const Common::String &anim, const Common::String &name, int textId);
	int chooseBridgeOption(const int16 *textIds, int count);
	void standingCrew(bool standing);
	void setMission(const Common::String &name);
	void missionFlyby(const Common::String &voice, const Common::String &title);
	void disableEnemyWeapons(R3 *ship, bool all);
	void enableEnemyWeapons(R3 *ship);
	void setAwayMissionResult(int16 score, int16 maxScore, int16 endMethod);
	void endTrialMission();
	void endOfGame();
	void startRandomEncounter();
	void randomEncounterHandler();
	void showMissionReview(int missionId, int textId, int score, int maxScore, bool removeTalker = true);

	// the action queue (DS:dc9a)
	Common::List<BridgeAction> _bridgeActions;
	// the mission's handler (DS:005e, kBridgeHandler...), its ticks (DS:bf60)
	// and flags (DS:bf6c)
	int16 _bridgeHandler = 0;
	uint32 _bridgeTicks = 0;
	int16 _missionFlags[6] = {};
	// the encounter's actions to wait before the hail (DS:adde)
	int16 _encounterCountdown = 0;
	// a cut-scene: the captain can't act (DS:0052)
	bool _bridgeInputDisabled = false;
	// the last away mission's score and its best (DS:b256, d1ce)
	int16 _awayMissionScore = 0;
	int16 _awayMissionMaxScore = 0;
	Common::String _viewscreenTalkerAnim;
	// the bridge's displays
	Sprite _displaySprites[5];
	Sprite _scannerSprite;
	Sprite _runningLightSprite;
	Bitmap *_phasindBitmap = nullptr;
	int _runningLight = 0;
	bool _bridgeDisplaysShown = false;
	bool _draggingSpeed = false;

private:
	Common::String getSpeechSampleForNumber(int number);
	void showTextboxBridge(int talker, int textId);
	void showTextboxBridge(int talker, Common::String text);
	void showBridgeScreenTalkerWithMessage(int textId, Common::String talkerHeader, Common::String talkerId, bool removeTalker = true);
	void showBridgeScreenTalkerWithMessages(Common::String texts[], Common::String talkerHeader, Common::String talkerId, bool removeTalker = true);
	void showMissionStartEnterpriseFlyby(Common::String sequence, Common::String name);
	void negotiateWithElasiCereth();

	int16 _targetPlanet;
	int16 _currentPlanet;
	int _currentScreenTalker;
	bool _gameIsPaused;
	bool _hailedTarget;
	int _deadMasadaPrisoners;
	bool _beamDownAllowed;
	int _missionEndFlag;
	int16 _randomEncounterType;	// 1: Klingon, 2: Romulan, 3: Elasi
	int16 _lastMissionId;
	int16 _missionPoints[7];

public:
	void playMovie(Common::String filename);
	void playMovieMac(Common::String filename);

	uint16 getRandomWord();

	// awaymission.cpp
	void initAwayMission();
	void runAwayMission();
	void cleanupAwayMission();
	void loadRoom(const Common::String &missionName, int roomIndex);
	void initAwayCrewPositions(int warpEntryIndex);
	void handleAwayMissionEvents();
	void awayMissionLeftClick();
	/**
	 * Called on right-click (select action), or when certain actions are otherwise
	 * selected (checks whether to show inventory icon, etc)
	 */
	void awayMissionSelectAction(bool openActionMenu);
	void awayMissionUseObject(int16 clickedObject);
	void awayMissionGetLookOrTalk(int16 clickedObject);
	void unloadRoom();
	/**
	 * Similar to loadActorAnim, but scale is determined by the y-position in the room. The
	 * further up (away) the object is, the smaller it is.
	 */
	int loadActorAnimWithRoomScaling(int actorIndex, const Common::String &animName, int16 x, int16 y);
	Fixed8 getActorScaleAtPosition(int16 y);
	void addAction(const Action &action);
	void addAction(int8 type, byte b1, byte b2, byte b3);
	void handleAwayMissionAction();

	void checkTouchedLoadingZone(int16 x, int16 y);
	/**
	 * Updates any nonzero away mission timers, and invokes ACTION_TIMER_EXPIRED when any one
	 * reaches 0.
	 */
	void updateAwayMissionTimers();
	/**
	 * Returns true if the given position in the room is solid (not walkable).
	 * Reads from a ".map" file which has a bit for each position in the room, which is true
	 * when that position is solid.
	 */
	bool isPositionSolid(int16 x, int16 y);
	void loadRoomIndex(int roomIndex, int spawnIndex);

	Room *getRoom();

	// intro.cpp
private:
	void playIntro();
	void showCreditsScreen(R3 *creditsBuffer, int index, bool deletePrevious = true);
	/**
	 * Initializes an object to spawn at one position and move toward another position.
	 * @param ticks The number of ticks it should take for the object to reach the destination
	 */
	void initIntroR3ObjectToMove(R3 *r3, int16 srcAngle, int16 srcDepth, int16 destAngle, int16 destDepth, int16 ticks);
	void loadSubtitleSprite(int index, Sprite *sprite);

	// space.cpp (pseudo-3d)
	void initStarfieldPosition();
	void initStarfield(int16 x1, int16 y1, int16 x2, int16 y2, int16 fov);
	void addR3(R3 *r3);
	void delR3(R3 *r3);
	void clearStarfieldPixels();
	void drawStarfield();
	void updateStarfieldAndShips(bool arg0);
	R3 *sub_19f24(R3 *r3);
	void drawR3Shape(R3 *r3);
	bool sub_1c022(R3 *r3);
	/**
	 * For a ship: loads the stored view of it ("ENT%02x.R3S" and so on) closest
	 * to the direction it's seen from (the original's funcPtr1 for ships).
	 */
	bool selectShipView(R3 *r3);
	void releaseShipView(R3 *r3);
	/// A planet (the original's 2d4b:0d7c): drawn from a fractal texture
	/// mapped onto a sphere (PLANET.DAT) as seen from the camera
	void initPlanetR3(R3 *r3, int16 size, int16 small, uint16 seed, uint16 color);
	bool selectPlanetView(R3 *r3);
	void freePlanetR3(R3 *r3);
	byte *_planetTables = nullptr; // PLANET.DAT
	/// The Enterprise as the original sets it up (ENTERPRI.DSC)
	void initEnterpriseR3();

	Point3 constructPoint3ForStarfield(int16 x, int16 y, int16 z);
	Point3 matrixMult(const Matrix &weight, const Point3 &point);
	Point3 matrixMult(const Point3 &point, const Matrix &weight);
	int32 scaleSpacePosition(int32 x, int32 z);

	/**
	 * Creates an identity matrix
	 */
	Matrix initMatrix();
	Matrix initSpeedMatrixForXZMovement(Angle angle, const Matrix &matrix);


	// actors.cpp (handles actors and animations)
public:
	void initActors();
	/**
	 * Set an actor's animation, position, and scale.
	 */
	int loadActorAnim(int actorIndex, const Common::String &animName, int16 x, int16 y, Fixed8 scale);
	void loadBanFile(const Common::String &name);
	/**
	 * Tries to make an actor walk to a position.
	 * Returns true if successful in initiating the walk.
	 */
	bool actorWalkToPosition(int actorIndex, const Common::String &animFile, int16 srcX, int16 srcY, int16 destX, int16 destY);
	void updateActorAnimations();

	/**
	 * ".BAN" files relate to drawing background animations, ie. flashing computer lights.
	 * "renderBanBelowSprites()" does the work of drawing it, while
	 * "renderBanAboveSprites()" redraws sprites above them if necessary.
	 */
	void renderBanBelowSprites();
	void renderBan(byte *screenPixels, byte *bgPixels, int banFileIndex);
	void renderBanAboveSprites();
	void removeActorFromScreen(int actorIndex);
	void removeDrawnActorsFromScreen();
	void drawActorToScreen(Actor *actor, const Common::String &animName, int16 x, int16 y, Fixed8 scale, bool addSprite);
	void releaseAnim(Actor *actor);
	void initStandAnim(int actorIndex);
	void updateActorPositionWhileWalking(Actor *actor, int16 x, int16 y);
	/**
	 * Chooses a value for the actor's speed and direction, based on a source position and
	 * a destination position it's walking to.
	 */
	void chooseActorDirectionForWalking(Actor *actor, int16 srcX, int16 srcY, int16 destX, int16 destY);
	/**
	 * Returns true if an actor can walk directly from a source position to a destination
	 * position without running into unwalkable terrain.
	 */
	bool directPathExists(int16 srcX, int16 srcY, int16 destX, int16 destY);

	int findObjectAt(int x, int y);
	int findObjectAt(Common::Point p) {
		return findObjectAt(p.x, p.y);
	}
	/**
	 * Loads a bitmap for the animation frame with the given scale.
	 */
	Bitmap *loadAnimationFrame(const Common::String &filename, Fixed8 scale);

	/**
	 * Called when the "get" action is first selected. Returns a selected object.
	 * This behaves like other menus in that it holds game execution, but no actual menu pops
	 * up; it just waits for the player to select something on the screen.
	 */
	int selectObjectForUseAction();
	Common::String getCrewmanAnimFilename(int actorIndex, const Common::String &basename);
	/**
	 * Checks whether to change the mouse bitmap to have the red outline.
	 */
	void updateMouseBitmap();
	/**
	 * Checks whether to walk a crewman to a hotspot (the last one obtained from
	 * a "findObjectAt" call).
	 */
	bool walkActiveObjectToHotspot();
	/**
	 * Return true if an object is unselectable with the given action?
	 */
	bool isObjectUnusable(int objectIndex, int action);
	/**
	 * When a crewman is collapsed, they get once a timer reaches 0.
	 */
	void updateCrewmanGetupTimers();
	void showInventoryIcons(bool showItem);
	void hideInventoryIcons();
	int showInventoryMenu(int x, int y, bool restoreMouse);
	void initStarfieldSprite(Sprite *sprite, Bitmap *bitmap, const Common::Rect &rect);
	Bitmap *scaleBitmap(Bitmap *bitmap, Fixed8 scale);
	/**
	 * This takes a row of an unscaled bitmap, and copies it to a row of a scaled bitmap.
	 * This was heavily optimized in the original game (manually constructed an unrolled
	 * loop).
	 */
	void scaleBitmapRow(byte *src, byte *dest, uint16 origWidth, uint16 scaledWidth);

	// events.cpp
public:
	/**
	 * Checks for all events, and updates Star Trek's event queue if queueEvents is set.
	 * This does not account for "tick" events (getNextEvent/popNextEvent handle that).
	 */
	void pollEvents(bool queueEvents = true);
	void waitForNextTick(bool queueEvents = true);
	void initializeEventsAndMouse();
	/**
	 * Returns false if there is no event waiting. If "poll" is true, this always returns
	 * something (waits until an event occurs if necessary).
	 */
	bool getNextEvent(TrekEvent *e, bool poll = true);
	void removeNextEvent();
	bool popNextEvent(TrekEvent *e, bool poll = true);
	void addEventToQueue(const TrekEvent &e);

	Common::EventManager *getEventMan() {
		return _eventMan;
	}

private:
	Common::List<TrekEvent> _eventQueue;
	bool _mouseMoveEventInQueue;
	bool _tickEventInQueue;
	uint32 _frameStartMillis;


	// textbox.cpp
public:
	/**
	 * Gets one line of text (does not include words that won't fit).
	 * Returns position of text to continue from, or nullptr if done.
	 */
	const char *getNextTextLine(const char *text, char *line, int lineWidth);
	/**
	 * Draw a line of text to a standard bitmap (NOT a "TextBitmap", whose pixel array is
	 * an array of characters, but an actual standard bitmap).
	 */
	void drawTextLineToBitmap(const char *text, int textLen, int x, int y, Bitmap *bitmap);

	Common::String centerTextboxHeader(Common::String headerText);
	void getTextboxHeader(Common::String *headerTextOutput, Common::String speakerText, int choiceIndex);
	/**
	 * Text getter for showText which reads from an rdf file.
	 * Not really used, since it would require hardcoding text locations in RDF files.
	 * "readTextFromArrayWithChoices" replaces this.
	 */
	Common::String readTextFromRdf(int choiceIndex, uintptr data, Common::String *headerTextOutput);

	/**
	 * Shows text with the given header and main text.
	 */
	void showTextbox(Common::String headerText, const Common::String &mainText, int xoffset, int yoffset, byte textColor, int maxTextLines); // TODO: better name. (return type?)

	Common::String skipTextAudioPrompt(const Common::String &str);
	/**
	 * Plays an audio prompt, if it exists, and returns the string starting at the end of the
	 * prompt.
	 */
	Common::String playTextAudio(const Common::String &str);

	/**
	 * @param rclickCancelsChoice   If true, right-clicks return "-1" as choice instead of
	 *                              whatever was selected.
	 */
	int showText(TextGetterFunc textGetter, uintptr var, int xoffset, int yoffset, int textColor, bool loopChoices, int maxTextLines, bool rclickCancelsChoice);

	/**
	 * Returns the number of lines this string will take up in a textbox.
	 */
	int getNumTextboxLines(const Common::String &str);
	Common::String putTextIntoLines(const Common::String &text);

	/**
	 * Creates a blank textbox in a TextBitmap, and initializes a sprite to use it.
	 */
	TextBitmap *initTextSprite(int *xoffsetPtr, int *yoffsetPtr, byte textColor, int numTextLines, bool withHeader, Sprite *sprite);
	/**
	 * Draws the "main" text (everything but the header at the top) to a TextBitmap.
	 */
	void drawMainText(TextBitmap *bitmap, int numTextLines, int numTextboxLines, const Common::String &text, bool withHeader);

	Common::String readLineFormattedText(TextGetterFunc textGetter, uintptr var, int choiceIndex, TextBitmap *textBitmap, int numTextboxLines, int *numLines);

	/**
	 * Text getter for showText which reads choices from an array of pointers.
	 * Last element in the array must be an empty string.
	 */
	Common::String readTextFromArray(int choiceIndex, uintptr data, Common::String *headerTextOutput);
	/**
	 * Similar to above, but shows the choice index when multiple choices are present.
	 * Effectively replaces the "readTextFromRdf" function.
	 */
	String readTextFromArrayWithChoices(int choiceIndex, uintptr data, Common::String *headerTextOutput);
	Common::String readTextFromFoundComputerTopics(int choiceIndex, uintptr data, Common::String *headerTextOutput);

	Common::String showCodeInputBox();
	Common::String showComputerInputBox();
	void redrawTextInput();
	void addCharToTextInputBuffer(char c);
	/**
	 * Shows a textbox that the player can type a string into.
	 */
	Common::String showTextInputBox(int16 arg0, int16 arg2, const Common::String &headerText);
	void initTextInputSprite(int16 arg0, int16 arg2, const Common::String &headerText);
	void cleanupTextInputSprite();

private:
	char _textInputBuffer[TEXT_INPUT_BUFFER_SIZE];
	int16 _textInputCursorPos;
	char _textInputCursorChar;
	Sprite _textInputSprite;

	// menu.cpp
public:
	/**
	 * Returns the index of the button at the given position, or -1 if none.
	 */
	int getMenuButtonAt(Sprite *sprites, int numSprites, int x, int y);
	/**
	 * This chooses a sprite from the list to place the mouse cursor at. The sprite it chooses
	 * may be, for example, the top-leftmost one in the list. Exact behaviour is determined by
	 * the "mode" parameter.
	 *
	 * If "containMouseSprite" is a valid index, it's ensured that the mouse is contained
	 * within it. "mode" should be -1 in this case.
	 */
	void chooseMousePositionFromSprites(Sprite *sprites, int numSprites, int spriteIndex, int mode);
	/**
	 * Draws or removes the outline on menu buttons when the cursor hovers on them, or leaves
	 * them.
	 */
	void drawMenuButtonOutline(Bitmap *bitmap, byte color);
	void showOptionsMenu(int x, int y);
	void showBridgeMenu(Common::String menu, int x, int y);
	void handleBridgeMenu(int menuEvent);
	void showStarMap();
	void orbitPlanet();
	void setOrbit(bool enter, bool say);
	void approachObject(int cls);
	void deleteApproachObject();

	// viewscreen.cpp: space as the bridge's viewscreen and the full screen
	// show it, and the 3D sequences
	void initSpaceObjects();
	/// The planet of a star system (kPlanet...), as the original makes it
	void setStarSystemPlanet(int planet);
	/// A random one (the mission start fly-bys' backdrop)
	void setRandomPlanet();
	void showFullScreenSpace();
	void showBridgeViewscreen();
	/// Space drawn as the camera sees it now
	void renderSpace(bool staticNoise = false);
	/// Each tick on the bridge: the Enterprise moving, the viewscreen
	void updateBridgeViewscreen();
	void enterOrbitSequence();
	void leaveOrbitSequence();

	// spacecombat.cpp: ships and their fights
	int combatRand();
	bool loadShipClass(R3 *r3, const Common::String &name);
	/// An enemy ship from its .DSC near the Enterprise (2f0d:0e49)
	R3 *spawnShip(const Common::String &name);
	void shipPower(R3 *r3);
	void moveShip(R3 *r3);
	void repairShip(R3 *r3);
	bool fireWeapon(R3 *r3, int weapon, const Point3 *dir);
	bool damageSystem(R3 *r3, int side, int16 damage);
	void hitShip(R3 *r3, R3 *shot);
	void moveShots();
	void enemyFire(R3 *r3, const Point3 &local, const Point3 &toEnterprise);
	bool shipManeuver(R3 *r3);
	void enemyShipTick(R3 *r3);
	void enemyShipsTick();
	void removeShots();
	bool boostPower();
	/// What the ships and shots do each tick in space
	void spaceCombatTick();
	Bitmap *combatShape(const Common::String &name);
	void freeCombatShapes();
	void drawShot(R3 *r3);
	void drawShipExplosion(R3 *r3);
	void drawPhaserBeam(R3 *r3);
	void drawViewscreenStatic();
	bool viewscreenStatic();
	const byte *cloakTable(int level);
	void handleSpaceEvent(int type, int data);
	void syncSpaceState(Common::Serializer &ser);
	void initBridgeDisplays();
	void removeBridgeDisplays();
	void updateBridgeDisplays(bool force = false);
	void updateScanner();
	void updateWeaponBar(int weapon, int x, int y, bool odd);
	void displayLine(int x, int y, int len, byte color);
	void displayBitmap(const Common::String &name, int recolor = -1);
	void steerEnterprise(Common::Point mouse);
	void centerEnterpriseControls();
	void setEnterpriseSpeed(int step);
	bool inSpaceView(Common::Point p);
	bool onSpeedBars(Common::Point p);
	void setSpeedFromBars(Common::Point p);
	void fireEnterpriseWeapons(bool torpedoes, bool phasers);
	void setRepairTarget(int16 target);
	void resetEnterpriseSystems();
	void stopShip(R3 *r3);
	/// A message of the space simulation, for the bridge (the original's
	/// event queue, 1e8e:17b9, read by 11dc:1c82)
	void addSpaceEvent(int type, int data);
	/// The star map: the star picked (its kPlanet...)
	int chooseStarMapDestination();
	/// Going to warp, to the star system set
	void warpSequence();
	void waitSpaceTick();
	void captainsLog();

	/**
	 * Show the "action selection" menu, ie. look, talk, etc.
	 */
	int showActionMenu();
	/**
	 * Loads a .MNU file, which is a list of buttons to display.
	 */
	void loadMenuButtons(String mnuFilename, int xpos, int ypos);
	/**
	 * Sets which buttons are visible based on the given bitmask.
	 */
	void setVisibleMenuButtons(uint32 bits);
	/**
	 * Disables the given bitmask of buttons.
	 */
	void disableMenuButtons(uint32 bits);
	void enableMenuButtons(uint32 bits);
	/**
	 * This returns either a special menu event (negative number) or the retval of the button
	 * clicked (usually an index, always positive).
	 */
	int handleMenuEvents(uint32 ticksUntilClickingEnabled, bool inTextbox);
	void unloadMenuButtons();

	/**
	 * Sets the mouse bitmap based on which action is selected.
	 */
	void chooseMouseBitmapForAction(int action, bool withRedOutline);
	void showQuitGamePrompt(int x, int y);
	void showGameOverMenu();
	/**
	 * This can be called from startup or from the options menu.
	 * On startup, this tries to load the setting without user input.
	 */
	void showTextConfigurationMenu(bool fromOptionMenu);

	int loadTextDisplayMode();
	void saveTextDisplayMode(int value);

	/**
	 * Show the republic map, only used in mission 7.
	 */
	void showRepublicMap(int16 arg0, int16 turbolift);
	/**
	 * Checks the mouse position to return and index for the area selected.
	 */
	int getRepublicMapAreaAtMouse();
	/**
	 * Same as above, but returns 6 or 7 as error conditions (can't reach due to radiation
	 * or wrong turbolift).
	 */
	int getRepublicMapAreaOrFailure(int16 turbolift);


private:
	int16 _textDisplayMode;
	uint32 _textboxVar2;
	uint16 _textboxVar6;
	bool _textboxHasMultipleChoices;
	Menu *_activeMenu;
	// Saved value of StarTrekEngine::_keyboardControlsMouse when menus are up
	bool _keyboardControlsMouseOutsideMenu;

	// saveload.cpp
public:
	bool showSaveMenu();
	bool showLoadMenu();

	bool saveGame(int slot, Common::String desc);
	bool loadGame(int slot);
	/// waiting for input at the top of the bridge / away mission loop
	bool _idle = false;

	/**
	 * Call this after loading "saveOrLoadMetadata" to load all the data pertaining to game
	 * execution.
	 */
	bool saveOrLoadGameData(Common::SeekableReadStream *in, Common::WriteStream *out, SavegameMetadata *meta);

	Common::String getSavegameFilename(int slotId) const;

	// detection.cpp
public:
	const StarTrekGameDescription *_gameDescription;
	uint32 getFeatures() const;
	Common::Platform getPlatform() const;
	uint8 getGameType() const;
	Common::Language getLanguage() const;

	// _screenName = _missionName + _roomIndex
	Common::String getScreenName() const {
		return _missionName + (char)(_roomIndex + '0');
	}

	// Variables
public:
	int _gameMode;
	int _lastGameMode;
	bool _resetGameMode;

	// NOTE: this has a different meaning than the original game. When non-empty, a new
	// room load is triggered, as opposed to original behaviour where this was only read
	// when "loadRoom" was called.
	Common::String _missionToLoad;
	int _roomIndexToLoad;
	int _spawnIndexToLoad;

	Common::String _missionName;
	int _roomIndex;
	Common::MemoryReadStreamEndian *_mapFile;
	Fixed16 _playerActorScale;

	// Queue of "actions" (ie. next frame, clicked on object) for away mission or bridge
	Common::Queue<Action> _actionQueue;

	AwayMission _awayMission;
	bool _warpHotspotsActive;
	int16 _activeWarpHotspot;
	int16 _activeDoorWarpHotspot;
	int16 _lookActionBitmapIndex;

	Item _itemList[NUM_OBJECTS];

	Actor _actorList[NUM_ACTORS];
	Actor *const _kirkActor;
	Actor *const _spockActor;
	Actor *const _mccoyActor;
	Actor *const _redshirtActor;

	// ".BAN" files provide extra miscellaneous animations in the room, ie. flashing
	// pixels on computer consoles, or fireflies in front of the screen.
	Common::MemoryReadStreamEndian *_banFiles[MAX_BAN_FILES];
	uint16 _banFileOffsets[MAX_BAN_FILES];

	Sprite _inventoryIconSprite;
	Sprite _itemIconSprite;

	// Certain hotspots store a position value where objects must walk to before
	// interacting with them. After calling "findObjectAt", these values are updated.
	bool _objectHasWalkPosition;
	Common::Point _objectWalkPosition;

	// Actions to perform after a crewman finishes walking to a position.
	// Room-specific code can specify that a specific action of type
	// "ACTION_FINISHED_WALKING" occurs after moving a crewman somewhere.
	Action _actionOnWalkCompletion[MAX_BUFFERED_WALK_ACTIONS];
	bool _actionOnWalkCompletionInUse[MAX_BUFFERED_WALK_ACTIONS];

	// _clockTicks is based on DOS interrupt 1A, AH=0; read system clock counter.
	// Updates 18.206 times every second.
	uint32 _clockTicks;
	uint32 _frameIndex;
	uint32 _roomFrameCounter; // Resets to 0 on loading a room

	bool _musicEnabled;
	bool _sfxEnabled;
	uint16 _word_467a6;
	uint16 _musicWorking;
	bool _sfxWorking;
	bool _finishedPlayingSpeech;

	bool _mouseControllingShip;

	// TODO: make this work.
	// When false, the keyboard generally acts in a more specific way (ie. move mouse
	// between items in a menu).
	bool _keyboardControlsMouse;

	bool _inQuitGameMenu;
	bool _showSubtitles;

	byte _byte_45b3c;

	// Pseudo-3D / starfield stuff
	Sprite _starfieldSprite;
	Star _starList[NUM_STARS];
	Point3 _starfieldPosition;
	int32 _starfieldPointDivisor;
	int16 _starfieldXVar1, _starfieldYVar1;
	int16 _starfieldXVar2, _starfieldYVar2;
	Common::Rect _starfieldRect;
	R3 _enterpriseR3;
	// The planet of the star system the Enterprise is in (the original's
	// DS:bf7e), drawn in space like the ships
	R3 _planetR3;
	// What the 3D view takes: the bridge's viewscreen (false) or the whole
	// screen (true, the original's DS:0054)
	bool _spaceFullScreen = false;
	// The camera is the Enterprise's (the bridge's viewscreen) unless a
	// sequence moves it itself
	bool _cameraOnEnterprise = true;
	Common::MemoryReadStreamEndian *_starsFile = nullptr;

	// spacecombat.cpp: the enemy ships (DS:b708) and the shots (DS:7420)
	R3 _enemyShips[NUM_ENEMY_SHIPS];
	// the station or ship approached (DS:beac)
	R3 _approachObject;
	R3 _shots[NUM_SHOTS];
	// target analysis' ship (DS:557e)
	R3 *_targetAnalysisShip = nullptr;
	// there are enemies (DS:005a)
	bool _inCombat = false;
	// what Scotty is repairing (DS:11ce): a system, or 0x7fff the shields, -1 nothing
	int16 _repairTarget = -1;
	// Scotty's extra power (DS:55e2..55ec)
	bool _extraPowerUsed = false;
	int32 _extraPowerTicks = 0;
	int16 _extraPower = 0;
	// what the commendations make of it (DS:55ea, 55ec)
	int16 _extraPowerBonus = 0x1e;
	int16 _extraPowerDuration = 0x1c2;
	Common::HashMap<Common::String, Bitmap *> _combatShapes;
	byte _cloakTable[256];
	int _cloakLevel = -1;
	// Insert / Delete held: roll (DS:d19e)
	int _rollKey = 0;
	R3 *_r3List[NUM_SPACE_OBJECTS];
	R3 *_orderedR3List[NUM_SPACE_OBJECTS];
	Matrix _starPositionMatrix;
	Matrix _someMatrix;
	float _flt_50898;

	Graphics *_gfx;
	Sound *_sound;
	IWFile *_iwFile;
	Resource *_resource;

	EnterpriseState _enterpriseState;

private:
	int leftClickEvent();
	int rightClickEvent();
	int mouseMoveEvent();
	int lookupNextAction(const int *lookupArray, int action);
	void loadBridgeComputerTopics();
	void bridgeLeftClick();

	Common::RandomSource _randomSource;
	Math::SineTable _sineTable;
	Math::CosineTable _cosineTable;
	Room *_room;
	Common::List<ComputerTopic> _computerTopics;
};

// Static function
bool saveOrLoadMetadata(Common::SeekableReadStream *in, Common::WriteStream *out, SavegameMetadata *meta);

} // End of namespace StarTrek

#endif
