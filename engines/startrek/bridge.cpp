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

#include "startrek/bridge.h"
#include "startrek/iwfile.h"
#include "startrek/resource.h"
#include "startrek/room.h"
#include "startrek/startrek.h"

namespace StarTrek {

#define ACTOR_STANDING_SPOCK "xstndsp"
#define ACTOR_STANDING_MCCOY "xstndmc"
#define ACTOR_SITTING_SPOCK "bstndsp"

struct BridgeActorAndMenu {
	int id;
	const char *anim;
	const char *menu;
	int16 x;
	int16 y;
};

BridgeActorAndMenu bridgeActorsAndMenus[] = {
	{ 0, "bstndki", "command",  60,  100 },	// Kirk
	{ 1, "bstndsp", "comp",    200,   30 },	// Spock
	{ 4, "bstndsu", "helm",     60,   70 },	// Sulu
	{ 5, "bstndch", "nav",      60,   70 },	// Chekov
	{ 6, "bstnduh", "comm",     30,   30 },	// Uhura
	{ 7, "bstndsc", "eng",      30,   30 },	// Scotty
};

void StarTrekEngine::initBridge(bool b) {
	_gfx->loadPalette("bridge");
	_sound->loadMusicFile("bridge");

	initStarfieldPosition();
	// TODO: starfield

	loadBridge();
}

void StarTrekEngine::loadBridge() {
	initStarfield(0x48, 0x1e, 0xf7, 0x66, 0x200 / 6);

	_gfx->setBackgroundImage("bridge");
	_gfx->loadPri("bridge");
	_gfx->copyBackgroundScreen();
	_system->updateScreen();

	loadBridgeActors();
	initStarfieldSprite(&_starfieldSprite, new StubBitmap(0, 0), _starfieldRect);
	renderSpace();
	initBridgeDisplays();

	//sub_1312C();	// TODO

	// TODO
	//initStarfieldSprite();
	//initStarfieldSprite();
	//initStarfieldSprite();
	//initStarfieldSprite();
	//initStarfieldSprite();
}

void StarTrekEngine::loadBridgeActors() {
	for (int i = 0; i < ARRAYSIZE(bridgeActorsAndMenus); ++i) {
		BridgeActorAndMenu a = bridgeActorsAndMenus[i];
		loadActorAnim(a.id, a.anim, 0, 0, 1.0);
	}
}

// TODO: 2 params, change Enterprise state
void StarTrekEngine::setBridgeMouseCursor() {
	_gfx->setMouseBitmap("pushbtn" /*_mouseControllingShip ? "entcur" : "cursor"*/);
}

void StarTrekEngine::showTextboxBridge(int talker, int textId) {
	showTextboxBridge(talker, _resource->getLoadedText(textId));
}

void StarTrekEngine::showTextboxBridge(int talker, Common::String text) {
	switch (talker) {
	case kBridgeTalkerKirk:
		showTextbox("Captain Kirk", text, 160, 130, 176, 0);
		break;
	case kBridgeTalkerSpock:
		showTextbox("Mr. Spock", text, 294, 106, 44, 0);
		break;
	case kBridgeTalkerSulu:
		showTextbox("Mr. Sulu", text, 122, 116, 176, 0);
		break;
	case kBridgeTalkerChekov:
		showTextbox("Mr. Chekov", text, 196, 116, 176, 0);
		break;
	case kBridgeTalkerUhura:
		showTextbox("Lieutenant Uhura", text, 298, 150, 161, 0);
		break;
	case kBridgeTalkerScotty:
		showTextbox("Mr. Scott", text, 64, 100, 161, 0);
		break;
	case kBridgeTalkerMcCoy:
		showTextbox("Dr. McCoy", text, 160, 100, 44, 0);
		break;
	case kBridgeTalkerCaptainsLog:
		showTextbox("Captain's Log", text, 160, 130, 176, 0);
		break;
	case kBridgeTalkerElasiCaptain:
		showTextbox("Elasi Captain", text, 160, 190, 44, 0);
		break;
	case kBridgeTalkerElasiCereth:
		showTextbox("Elasi Cereth", text, 160, 190, 44, 0);
		break;
	case kBridgeTalkerAutobeacon:
		showTextbox("Autobeacon", text, 160, 130, 161, 0);
		break;
	case kBridgeTalkerHarryMuddsVoice:
		showTextbox("Voice", text, 160, 130, 44, 0);
		break;
	case kBridgeTalkerHarryMudd:
		showTextbox("Harry Mudd", text, 160, 190, 44, 0);
		break;
	case kBridgeTalkerTaraz:
		showTextbox("Taraz", text, 160, 190, 44, 0);
		break;
	case kBridgeTalkerAdmiralKenka:
		showTextbox("Admiral Kenka", text, 160, 190, 44, 0);
		break;
	case kBridgeTalkerKallarax:
		showTextbox("Kallarax", text, 160, 190, 44, 0);
		break;
	}
}

void StarTrekEngine::negotiateWithElasiCereth() {
	// TODO: Read these from tug.txt
	const char *options1[] = {
	    "Captain Kirk",
		"#BRID\\C_074#This is Captain James T. Kirk of the U.S.S. Enterprise. You are illegally in possession of Starfleet property.",
		"#BRID\\C_072#This is Captain James T. Kirk of the U.S.S. Enterprise. Listen, Elasi, hand over the ship and hostages now or things are going to get very nasty.",
		"#BRID\\C_073#This is Captain James T. Kirk of the U.S.S. Enterprise. Mr. Elasi, it appears that you have found something that Starfleet lost.",
		""
	};

	const char *options2[] = {
	    "Captain Kirk",
		"#BRID\\C_026#Freeing the prisoners you requested will take time. We have to track down their current locations, and Starfleet will have to grant permission to move them here. I will get that started - you should tender a show of good faith on your part. Will you beam over so we can discuss negotiations face to face?",
		"#BRID\\C_091#We can work together on this, Elasi. How about beaming some of the hostages over as a show of good faith?",
		""
	};

	const char *options3[] = {
	    "Captain Kirk",
		"#BRID\\C_077#Very well, we'll do it your way. I will put together the data packet.  Kirk out.",
		"#BRID\\C_102#Your fellow Elasi have been tried in Federation courts of law, and found guilty of their crimes. We'll listen to your complaints if you discuss them reasonably.",
	    "#BRID\\C_014#Forget it. Elasi Cereth, you'll be the next one languishing on a Federation penal planet unless you drop your shields and surrender to me immediately.",
		""
	};

	int choice = showText(&StarTrekEngine::readTextFromArrayWithChoices, (uintptr)options1, 160, 130, 176, true, false, false);
	if (choice == 0) {
		showTextboxBridge(kBridgeTalkerElasiCereth, 44);	// Ah, the infamous captain Kirk...
		showTextboxBridge(kBridgeTalkerUhura, 45);
	} else if (choice == 1) {
		showTextboxBridge(kBridgeTalkerElasiCereth, 46);	// Well, Kirk, someone is about to pay the price for your insolence
		showTextboxBridge(kBridgeTalkerSpock, 60);
		_missionFlags[0]++;
	} else if (choice == 2) {
		showTextboxBridge(kBridgeTalkerElasiCereth, 47);
		showTextboxBridge(kBridgeTalkerElasiCereth, 44); // Ah, the infamous captain Kirk...
		showTextboxBridge(kBridgeTalkerUhura, 45);
	}

	showText(&StarTrekEngine::readTextFromArrayWithChoices, (uintptr)options2, 160, 130, 176, true, false, false);
	// The choice here makes no difference
	showTextboxBridge(kBridgeTalkerElasiCereth, 51);
	showTextboxBridge(kBridgeTalkerScotty, 52);
	showTextboxBridge(kBridgeTalkerSulu, 53);
	showTextboxBridge(kBridgeTalkerElasiCereth, 54);

	choice = showText(&StarTrekEngine::readTextFromArrayWithChoices, (uintptr)options3, 160, 130, 176, true, false, false);
	if (choice == 0) {
		// End of dialog
	} else if (choice == 1) {
		showTextboxBridge(kBridgeTalkerElasiCereth, 69);
	} else if (choice == 2) {
		showTextboxBridge(kBridgeTalkerElasiCereth, 70);
		showTextboxBridge(kBridgeTalkerSpock, 60);
		_missionFlags[0]++;
		showTextboxBridge(kBridgeTalkerElasiCereth, 71);
		// The original checks for 4 dead prisoners here, but that is impossible at this point
	}

	// There are some more checks for dialog choices here, but all seem to be dead code
}

Common::String StarTrekEngine::getSpeechSampleForNumber(int number) {
	const char *speechTemplate = ",BRID\\B_%03d";
	Common::String result;

	if (number <= 19) {
		// Speech samples 228 ("zero") - 247 ("nineteen")
		result = Common::String::format(speechTemplate, number + 228);
	} else if (number >= 20 && number <= 99) {
		// Speech samples 248 ("twenty") - 255 ("ninety")
		result = Common::String::format(speechTemplate, number / 10 - 2 + 248);
		if (number % 10 > 0)
			result += Common::String::format(speechTemplate, number % 10 + 228);
	} else if (number == 100) {
		result = Common::String::format(speechTemplate, 256);
	}

	return result;
}

void StarTrekEngine::showBridgeScreenTalkerWithMessage(int textId, Common::String talkerHeader, Common::String talkerId, bool removeTalker) {
	Common::String text = _resource->getLoadedText(textId);
	Common::String texts[] = {
		text,
		""
	};
	showBridgeScreenTalkerWithMessages(texts, talkerHeader, talkerId, removeTalker);
}

void StarTrekEngine::showBridgeScreenTalkerWithMessages(Common::String texts[], Common::String talkerHeader, Common::String talkerId, bool removeTalker) {
	if (talkerId == "romula" || talkerId == "pira" || talkerId == "klg1" || talkerId == "klg2" || talkerId == "maddoc")
		_sound->playMidiMusicTracks(MIDITRACK_15);
	else if (talkerId == "mudd")
		_sound->playMidiMusicTracks(MIDITRACK_17);

	initStarfieldSprite(&_starfieldSprite, new Bitmap(_resource->loadBitmapFile(talkerId)), _starfieldRect);
	_starfieldSprite.drawMode = 0;
	_currentScreenTalker = loadActorAnim(-1, talkerId, 72 + _starfieldSprite.bitmap->xoffset - 2, 30 + _starfieldSprite.bitmap->yoffset, 1.0);
	int i = 0;
	Common::String text = texts[i];
	while (text != "") {
		showTextbox(talkerHeader, text, 160, 190, 44, 0);
		text = texts[++i];
	}

	if (removeTalker) {
		removeActorFromScreen(_currentScreenTalker);
		initStarfieldSprite(&_starfieldSprite, new StubBitmap(0, 0), _starfieldRect);
	}
}

void StarTrekEngine::bridgeLeftClick() {
	Sprite *sprite = _gfx->getSpriteAt(_gfx->getMousePos());
	int clickedActor = -1;

	if (sprite == nullptr)
		return;

	for (int i = 0; i < NUM_ACTORS; i++) {
		Actor *actor = &_actorList[i];
		if (sprite == &actor->sprite) {
			clickedActor = i;
			break;
		}
	}

	if (clickedActor == -1)
		return;

	for (int i = 0; i < ARRAYSIZE(bridgeActorsAndMenus); ++i) {
		BridgeActorAndMenu a = bridgeActorsAndMenus[i];
		if (a.id == clickedActor) {
			showBridgeMenu(a.menu, a.x, a.y);
			break;
		}
	}
}

void StarTrekEngine::cleanupBridge() {
	removeBridgeDisplays();
	// TODO
	// if (!v_starfieldInitialized)
	// sub_12691()
	// else
	// clearScreenAndDelSeveralSprites()
	// v_mouseControllingShip = 0
	// v_keyboardControlsMouse = 1
}

void StarTrekEngine::runBridge() {
	while (_gameMode == GAMEMODE_BRIDGE && !_resetGameMode && !shouldQuit()) {
		handleBridgeEvents();
		runBridgeActions();
	}
}

void StarTrekEngine::handleBridgeEvents() {
	TrekEvent event;

	_idle = true;
	const bool gotEvent = popNextEvent(&event);
	_idle = false;
	if (gotEvent) {
		if (_gameIsPaused) {
			if (event.type == TREKEVENT_LBUTTONDOWN || event.type == TREKEVENT_KEYDOWN) {
				_gameIsPaused = false;
			}
			return;
		}

		// (while the Enterprise explodes, or in a cut-scene, only time
		// passes)
		if ((_enterpriseR3.ship.destroyed || _bridgeInputDisabled) && event.type != TREKEVENT_TICK)
			return;

		switch (event.type) {
		case TREKEVENT_TICK:
			// a tick for the mission and the bridge (11dc:042c)
			_frameIndex++;
			_bridgeTicks++;
			postBridgeAction(kBridgeActionTick, 0);
			break;

		case TREKEVENT_LBUTTONDOWN:
			// on the speed bars, the speed (for touch: the original has only
			// the keys); on the viewscreen the captain flies and fires (the
			// original's ship mode); elsewhere he picks his crew
			if (onSpeedBars(_gfx->getMousePos())) {
				_draggingSpeed = true;
				setSpeedFromBars(_gfx->getMousePos());
			} else if (inSpaceView(_gfx->getMousePos())) {
				steerEnterprise(_gfx->getMousePos());
				postBridgeAction(kBridgeActionMenu, kBridgeUnk1);
			} else {
				bridgeLeftClick();
			}
			break;

		case TREKEVENT_LBUTTONUP:
			_draggingSpeed = false;
			break;

		case TREKEVENT_MOUSEMOVE:
			if (_draggingSpeed)
				setSpeedFromBars(_gfx->getMousePos());
			else if (inSpaceView(_gfx->getMousePos()))
				steerEnterprise(_gfx->getMousePos());
			break;

		case TREKEVENT_RBUTTONDOWN:
			if (inSpaceView(_gfx->getMousePos()))
				postBridgeAction(kBridgeActionMenu, kBridgeUnk2);
			break;

		case TREKEVENT_KEYDOWN:
			switch (event.kbd.keycode) {
			case Common::KEYCODE_w:	// Toggle weapons on/off
				postBridgeAction(kBridgeActionMenu, kBridgeChekovWeapons);
				break;
			case Common::KEYCODE_s:	// Toggle shields on/off
				postBridgeAction(kBridgeActionMenu, kBridgeSuluShields);
				break;
			case Common::KEYCODE_a:	// Target analysis
				postBridgeAction(kBridgeActionMenu, kBridgeSuluTargetAnalysis);
				break;
			case Common::KEYCODE_d:	// Damage control
				postBridgeAction(kBridgeActionMenu, kBridgeScottyDamageControl);
				break;
			case Common::KEYCODE_RETURN:
			case Common::KEYCODE_KP_ENTER:
			case Common::KEYCODE_F1:
			case Common::KEYCODE_KP_PLUS:
				// Fire phasers
				postBridgeAction(kBridgeActionMenu, kBridgeUnk1);
				break;
			case Common::KEYCODE_SPACE:	// Fire photon torpedoes
			case Common::KEYCODE_F2:
			case Common::KEYCODE_KP_MINUS:
				postBridgeAction(kBridgeActionMenu, kBridgeUnk2);
				break;
			case Common::KEYCODE_v:	// Toggle between the normal bridge view and full screen view
				postBridgeAction(kBridgeActionMenu, kBridgeStarfieldFullScreen);
				break;
			case Common::KEYCODE_TAB:	// Toggle between movement/fire mode and crew selection mode
				// TODO
				break;
			case Common::KEYCODE_COMMA:	// Reduce the main view screen magnification
				// TODO
				break;
			case Common::KEYCODE_PERIOD:	// Enlarge the main view screen magnification
				// TODO
				break;
			case Common::KEYCODE_o:	// Enter/exit orbit
				postBridgeAction(kBridgeActionMenu, kBridgeSuluOrbit);
				break;
			case Common::KEYCODE_KP5:	// Center controls
				centerEnterpriseControls();
				break;
			case Common::KEYCODE_n:	// Main star navigational map
				postBridgeAction(kBridgeActionMenu, kBridgeChekovNavigation);
				break;
			case Common::KEYCODE_k:	// Kirk's options
				postBridgeAction(kBridgeActionMenu, kBridgeKirkOptions);
				break;
			case Common::KEYCODE_t:	// Ask Mr. Spock for advice
				postBridgeAction(kBridgeActionMenu, kBridgeSpock);
				break;
			case Common::KEYCODE_c:	// Spock's library computer
				postBridgeAction(kBridgeActionMenu, kBridgeSpockComputer);
				break;
			case Common::KEYCODE_h:	// Uhura's communication icon
				postBridgeAction(kBridgeActionMenu, kBridgeUhura);
				break;
			case Common::KEYCODE_p:	// Pause game
				_gameIsPaused = true;
				break;
			case Common::KEYCODE_e:	// Toggle SFX / emergency power
				if (event.kbd.flags & Common::KBD_CTRL)
					_sound->toggleSfx();
				else
					postBridgeAction(kBridgeActionMenu, kBridgeScottyEmergencyPower);
				break;
			case Common::KEYCODE_m:
				if (event.kbd.flags & Common::KBD_CTRL)
					_sound->toggleMusic();
				break;
			case Common::KEYCODE_q:
				if (event.kbd.flags & Common::KBD_CTRL)
					showQuitGamePrompt(20, 20);
				break;
			case Common::KEYCODE_TILDE:	// Ship speed: reverse
			case Common::KEYCODE_BACKQUOTE:
				setEnterpriseSpeed(-1);
				break;
			case Common::KEYCODE_1:	// Ship speed: stop
				setEnterpriseSpeed(0);
				break;
			case Common::KEYCODE_2:
			case Common::KEYCODE_3:
			case Common::KEYCODE_4:
			case Common::KEYCODE_5:
			case Common::KEYCODE_6:
			case Common::KEYCODE_7:
			case Common::KEYCODE_8:
			case Common::KEYCODE_9:
			case Common::KEYCODE_0:
				// Ship speed: toggle slow -> fast
				setEnterpriseSpeed(event.kbd.keycode == Common::KEYCODE_0 ? 9 : event.kbd.keycode - Common::KEYCODE_1);
				break;
			default:
				break;
			}
			break;

		default:
			break;
		}
	}
}

// The crew's orders, as the bridge answers them when the mission doesn't
// (11dc:2128)
void StarTrekEngine::handleBridgeMenu(int menuEvent) {
	const char *underAttackText = "#BRID\\B_348#Unable to comply, Captain.  We're under attack.";
	const char *missionNotOverText = "#BRID\\B_338#May I respectfully remind the Captain that we haven't accomplished our mission, Sir.";
	const char *leaveOrbitText = "#BRID\\B_349#We must first leave orbit, Sir.";
	const char *nothingToReportText = "#BRID\\B_155#Nothing to report, Captain.";
	const char *hailingFrequenciesOpenText = "#BRID\\BRIDU016#Hailing frequencies open.";
	const char *shieldsUpText = "#BRID\\B_332#Captain, the shields are up.";
	const char *notInOrbitText = "#BRID\\B_350#We're not in orbit, Captain.";
	const char *wrongDestinationText = "#BRID\\B_346#This isn't our destination.";
	const char *transporterText = "#BRID\\C_060#Spock, come with me. Mr Scott, you have the conn.";
	const char *transporterTextFeather = "#BRID\\C_006#Assemble a landing party. Unless we find this so-called criminal, we're going to war.";
	const char *raiseShieldsText = "#BRID\\B_340#Raising shields.";
	const char *lowerShieldsText = "#BRID\\B_337#Lowering shields, Captain.";
	const char *armWeaponsText = "#BRID\\B_351#Arming weapons.";
	const char *disarmWeaponsText = "#BRID\\B_354#Disarming weapons.";
	const char *emergencyPowerText = "#BRID\\BRID_S32#I don't know how long she can take it, Captain.";
	const char *noEmergencyPowerText = "#BRID\\BRID_S41#She can't take it, Captain.";
	const char *targetAnalysisOnText = "#BRID\\B_344#Target Analysis On.";
	const char *targetAnalysisOffText = "#BRID\\B_343#Target Analysis Off.";
	const char *ayeSirText = "#BRID\\BRID_S22#Aye Sir.";

	switch (menuEvent) {
	case kBridgeKirkCommand:
		showBridgeMenu("command", 60, 100);
		break;
	case kBridgeUnk1: // fire phasers
		fireEnterpriseWeapons(false, true);
		break;
	case kBridgeUnk2: // fire photon torpedoes
		fireEnterpriseWeapons(true, false);
		break;
	case kBridgeStarfieldFullScreen: // the bridge or space across the screen
		if (_spaceFullScreen)
			showBridgeViewscreen();
		else
			showFullScreenSpace();
		break;
	case kBridgeKirkCaptainsLog:
		captainsLog();
		break;
	case kBridgeKirkTransporter:
		if (_enterpriseR3.ship.shieldsUp) {
			showTextboxBridge(kBridgeTalkerSulu, shieldsUpText);
		} else if (!_enterpriseState.inOrbit) {
			showTextboxBridge(kBridgeTalkerSulu, notInOrbitText);
		} else if (_currentPlanet != _targetPlanet) {
			showTextboxBridge(kBridgeTalkerSulu, wrongDestinationText);
		} else {
			if (_missionName == "FEATHER")
				showTextboxBridge(kBridgeTalkerKirk, transporterTextFeather);
			else if (_missionName != "VENG")
				showTextboxBridge(kBridgeTalkerKirk, transporterText);
			_gameMode = GAMEMODE_BEAMDOWN;
		}
		break;
	case kBridgeKirkOptions:
		showOptionsMenu(65, 60);
		break;
	case kBridgeSpock:
		showTextboxBridge(kBridgeTalkerSpock, nothingToReportText);
		break;
	case kBridgeSpockComputer:
		handleBridgeComputer();
		break;
	case kBridgeScottyDamageControl:
		showBridgeMenu("repair", 30, 30);
		break;
	case kBridgeScottyEmergencyPower: // (2f0d:30ef)
		showTextboxBridge(kBridgeTalkerScotty, boostPower() ? emergencyPowerText : noEmergencyPowerText);
		break;
	case kBridgeUhura:
		showTextboxBridge(kBridgeTalkerUhura, hailingFrequenciesOpenText);
		break;
	case kBridgeSuluOrbit:
		if (_inCombat)
			showTextboxBridge(kBridgeTalkerSulu, underAttackText);
		else if (_currentPlanet == _targetPlanet && !_enterpriseState.inOrbit)
			setOrbit(true, true);
		else
			showTextboxBridge(kBridgeTalkerSulu, missionNotOverText);
		break;
	case kBridgeSuluShields:
		_enterpriseState.shields = !_enterpriseState.shields;
		showTextboxBridge(kBridgeTalkerSulu, _enterpriseState.shields ? raiseShieldsText : lowerShieldsText);
		break;
	case kBridgeChekovNavigation:
		if (_inCombat) {
			showTextboxBridge(kBridgeTalkerSulu, underAttackText);
		} else if (_currentPlanet == _targetPlanet) {
			showTextboxBridge(kBridgeTalkerSulu, missionNotOverText);
		} else if (_enterpriseState.inOrbit) {
			showTextboxBridge(kBridgeTalkerSulu, leaveOrbitText);
		} else {
			showStarMap();
			// the wrong star: an encounter (11dc:330a)
			if (_currentPlanet != _targetPlanet)
				startRandomEncounter();
		}
		break;
	case kBridgeChekovWeapons:
		_enterpriseState.weapons = !_enterpriseState.weapons;
		showTextboxBridge(kBridgeTalkerChekov, _enterpriseState.weapons ? armWeaponsText : disarmWeaponsText);
		setBridgeMouseCursor();
		break;
	case kBridgeChekovRepairShields:
	case kBridgeChekovRepairPhasers:
	case kBridgeChekovRepairPhotonTorpedoes:
	case kBridgeChekovRepairSensors:
	case kBridgeChekovRepairBridge:
	case kBridgeChekovRepairHull:
	case kBridgeChekovRepairWarpDrives: {
		// what Scotty mends first (2f0d:192c)
		static const int16 kRepairTargets[] = { 0x7fff, 4, 2, 6, 1, 0, 7 };
		setRepairTarget(kRepairTargets[menuEvent - kBridgeChekovRepairShields]);
		showTextboxBridge(kBridgeTalkerScotty, ayeSirText);
		break;
	}
	case kBridgeSuluTargetAnalysis:
		_enterpriseState.targetAnalysis = !_enterpriseState.targetAnalysis;
		showTextboxBridge(kBridgeTalkerSulu, _enterpriseState.targetAnalysis ? targetAnalysisOnText : targetAnalysisOffText);
		// the first enemy, then whichever the Enterprise last hit
		_targetAnalysisShip = nullptr;
		if (_enterpriseState.targetAnalysis) {
			for (int i = 0; i < NUM_ENEMY_SHIPS; i++) {
				if (_enemyShips[i].field1e != 0) {
					_targetAnalysisShip = &_enemyShips[i];
					break;
				}
			}
		}
		break;
	default:
		break;
	}
}

// Chekov, navigation: the star map, then warp (11dc:269c)
void StarTrekEngine::showStarMap() {
	_currentPlanet = chooseStarMapDestination();
	showBridgeViewscreen();
	renderSpace();
	_gfx->fadeinScreen();
}

void StarTrekEngine::captainsLog() {
	const char *missionNames[] = {
		"Demon World",
		"Hijacked",
		"Love's Labor Jeopardized",
		"Another Fine Mess",
		"Feathered Serpent",
		"That Old Devil Moon",
		"Vengeance"
	};

	int totalPoints = 0;
	for (int i = 0; i < 7; i++)
		totalPoints += _missionPoints[i];

	Common::String captainsLogMessage = "";

	if (_lastMissionId > -1) {
		captainsLogMessage = Common::String::format(
			"#BRID\\C_007#We have earned %d point",
			totalPoints
		);

		if (totalPoints > 1)
			captainsLogMessage += "s";

		captainsLogMessage += " for the following recently completed missions:\n";

		for (int i = 6; i >= 0; i--) {
			captainsLogMessage += missionNames[i] + Common::String::format(":   %d point", _missionPoints[i]);
			if (_missionPoints[i] > 1)
				captainsLogMessage += "s";
			captainsLogMessage += ".\n\n";
		}
	} else {
		captainsLogMessage = "#BRID\\C_007#No recent missions have been completed.";
	}

	showTextboxBridge(kBridgeTalkerCaptainsLog, captainsLogMessage);
}

void StarTrekEngine::loadBridgeComputerTopics() {
	Common::MemoryReadStreamEndian *indexFile = _resource->loadFile("db.key");
	Common::String line;
	while (!indexFile->eos() && !indexFile->err()) {
		line = indexFile->readLine();
		if (line == "$")
			break;
		ComputerTopic topic;
		topic.fileName = line.substr(0, 7);
		topic.topic = line.substr(8);
		_computerTopics.push_back(topic);
	}
	delete indexFile;
}

void StarTrekEngine::handleBridgeComputer() {
	Common::String line;
	Common::String input = showComputerInputBox();
	if (input.size() < 3)
		return;

	char topics[10][8];
	int topicIndex = 0;

	memset(topics, 0, ARRAYSIZE(topics) * 8);

	for (Common::List<ComputerTopic>::const_iterator i = _computerTopics.begin(), end = _computerTopics.end(); i != end; ++i) {
		if (i->topic.hasPrefixIgnoreCase(input)) {
			memcpy(topics[topicIndex++], i->fileName.c_str(), i->fileName.size());
			if (topicIndex >= ARRAYSIZE(topics))
				break;
		}
	}

	showText(&StarTrekEngine::readTextFromFoundComputerTopics, (uintptr)topics, 20, 20, TEXTCOLOR_YELLOW, true, false, true);
}

} // End of namespace StarTrek
