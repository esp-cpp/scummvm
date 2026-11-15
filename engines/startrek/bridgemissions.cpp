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

// The missions on the bridge, as the original (DOS CD) runs them: each
// mission's state is a handler (DS:005e) offered every action of the
// bridge's queue (DS:dc9a): the ticks, the crew's orders, text boxes
// closing, the return from an away mission. It does its own timed talk and
// watches its battles, and either takes the action or leaves it to the
// bridge's default (11dc:1c82). Handlers are numbered as the original's
// (DS:006c, kBridgeHandler...), and named by address.

#include "startrek/bridge.h"
#include "startrek/resource.h"
#include "startrek/sound.h"
#include "startrek/startrek.h"

namespace StarTrek {

// ---- The action queue (DS:dc9a) and its handlers ----

void StarTrekEngine::postBridgeAction(int type, int data) {
	if (_bridgeActions.size() < 16) {
		BridgeAction a;
		a.type = type;
		a.data = data;
		_bridgeActions.push_back(a);
	}
}

void StarTrekEngine::clearBridgeActions() {
	_bridgeActions.clear();
}

void StarTrekEngine::swallowBridgeAction() {
	if (!_bridgeActions.empty())
		_bridgeActions.pop_front();
}

void StarTrekEngine::setBridgeHandler(int handler) {
	_bridgeHandler = handler;
	_bridgeTicks = 0;
}

// Offer each queued action to the mission (or, during a random encounter,
// to the encounter: 11dc:042c)
void StarTrekEngine::runBridgeActions() {
	int guard = 0;
	while (!_bridgeActions.empty() && _gameMode == GAMEMODE_BRIDGE && !shouldQuit()) {
		uint size = _bridgeActions.size();
		BridgeAction head = _bridgeActions.front();
		int handler = _bridgeHandler;
		if (_randomEncounterType != kRandomEncounterNone)
			randomEncounterHandler();
		else
			runBridgeHandler();
		// (a handler that neither took the action nor changed state: the
		// original would offer it again forever)
		if (_bridgeActions.size() == size && !_bridgeActions.empty() &&
				_bridgeActions.front().type == head.type && _bridgeActions.front().data == head.data &&
				_bridgeHandler == handler) {
			if (++guard > 4) {
				warning("bridge handler %d left action %d/%d", handler, head.type, head.data);
				swallowBridgeAction();
				guard = 0;
			}
		} else {
			guard = 0;
		}
	}
}

// The bridge's own response (11dc:1c82)
void StarTrekEngine::bridgeDefaultAction() {
	if (_bridgeActions.empty())
		return;
	BridgeAction a = _bridgeActions.front();
	_bridgeActions.pop_front();
	switch (a.type) {
	case kBridgeActionTick:
		bridgeTick();
		break;
	case kBridgeActionMenu:
		handleBridgeMenu(a.data);
		break;
	case kSpaceEventShieldsDown:
	case kSpaceEventWeaponsDown:
	case kSpaceEventRepaired:
		handleSpaceEvent(a.type, a.data);
		break;
	default:
		break;
	}
}

// Each tick: space, then the bridge redrawn (11dc:1c82 event 0)
void StarTrekEngine::bridgeTick() {
	updateActorAnimations();
	updateBridgeViewscreen();
	if (!_spaceFullScreen)
		updateBridgeDisplays();
	renderBanBelowSprites();
	_gfx->drawAllSprites(false);
	renderBanAboveSprites();
	_gfx->updateScreen();
	_sound->checkLoopMusic();
	_roomFrameCounter++;
	if (_roomFrameCounter >= 2)
		_gfx->incPaletteFadeLevel();
}

// ---- Things the handlers do ----

// A crewman says it; a closing box with a flag queues an action 5
// (1e8e:3116)
void StarTrekEngine::bridgeText(int talker, int textId, int flag) {
	bridgeText(talker, _resource->getLoadedText(textId), flag);
}

void StarTrekEngine::bridgeText(int talker, const Common::String &text, int flag) {
	showTextboxBridge(talker, text);
	if (flag > 0)
		postBridgeAction(kBridgeActionText, flag);
}

// Someone on the viewscreen (11dc:28d1): the hails' music, their animation
void StarTrekEngine::viewscreenTalker(const Common::String &anim) {
	if (anim == "romula" || anim == "pira" || anim == "klg1" || anim == "klg2" || anim == "maddoc")
		_sound->playMidiMusicTracks(MIDITRACK_15, kLoopTypeLast);
	else if (anim == "mudd")
		_sound->playMidiMusicTracks(MIDITRACK_17, kLoopTypeLast);
	initStarfieldSprite(&_starfieldSprite, new Bitmap(_resource->loadBitmapFile(anim)), _starfieldRect);
	_starfieldSprite.drawMode = 0;
	_viewscreenTalkerAnim = anim;
	_currentScreenTalker = loadActorAnim(-1, anim, 72 + _starfieldSprite.bitmap->xoffset - 2, 30 + _starfieldSprite.bitmap->yoffset, 1.0);
}

void StarTrekEngine::viewscreenTalkerText(const Common::String &name, int textId) {
	showTextbox(name, _resource->getLoadedText(textId), 160, 190, 44, 0);
}

// (11dc:29f2) its animation again
void StarTrekEngine::restartViewscreenTalker() {
	loadActorAnim(_currentScreenTalker, _viewscreenTalkerAnim, 72 + _starfieldSprite.bitmap->xoffset - 2, 30 + _starfieldSprite.bitmap->yoffset, 1.0);
}

// (11dc:2a12)
void StarTrekEngine::removeViewscreenTalker() {
	removeActorFromScreen(_currentScreenTalker);
	initStarfieldSprite(&_starfieldSprite, new StubBitmap(0, 0), _starfieldRect);
	renderSpace();
}

// (11dc:2a59)
void StarTrekEngine::viewscreenMessage(const Common::String &anim, const Common::String &name, int textId) {
	viewscreenTalker(anim);
	viewscreenTalkerText(name, textId);
	removeViewscreenTalker();
}

// Kirk's choice of the texts given (1e8e:32ac)
int StarTrekEngine::chooseBridgeOption(const int16 *textIds, int count) {
	Common::Array<Common::String> texts;
	for (int i = 0; i < count; i++)
		texts.push_back(_resource->getLoadedText(textIds[i]));
	Common::Array<const char *> array;
	array.push_back("Captain Kirk");
	for (uint i = 0; i < texts.size(); i++)
		array.push_back(texts[i].c_str());
	array.push_back("");
	return showText(&StarTrekEngine::readTextFromArrayWithChoices, (uintptr)array.data(), 160, 130, 176, true, false, false);
}

// Spock and McCoy standing, as at a mission's end
void StarTrekEngine::standingCrew(bool standing) {
	if (standing) {
		loadActorAnim(1, "xstndsp", 0, 0, 1.0);
		loadActorAnim(2, "xstndmc", 0, 0, 1.0);
	} else {
		loadActorAnim(1, "bstndsp", 0, 0, 1.0);
		removeActorFromScreen(2);
	}
}

void StarTrekEngine::setMission(const Common::String &name) {
	_missionName = _missionToLoad = name;
	_resource->setTxtFileName(name);
	for (int i = 0; i < 6; i++)
		_missionFlags[i] = 0;
}

void StarTrekEngine::missionFlyby(const Common::String &voice, const Common::String &title) {
	showMissionStartEnterpriseFlyby(voice, title);
	// (the state's ticks go on from the fly-by's)
	_bridgeTicks = 0x96;
}

void StarTrekEngine::disableEnemyWeapons(R3 *ship, bool all) {
	for (int i = 0; i < 4; i++) {
		if (all || i != 1)
			ship->ship.systems[kSystemWeapon0 + i].enabled = 0;
	}
}

void StarTrekEngine::enableEnemyWeapons(R3 *ship) {
	ship->ship.systems[kSystemWeapon0].enabled = 1;
	ship->ship.systems[kSystemWeapon0 + 2].enabled = 1;
	ship->ship.systems[kSystemWeapon0 + 3].enabled = 1;
}

static bool hullBelowHalf(const R3 &ship) {
	return ship.ship.systems[kSystemHull].cur < ship.ship.systems[kSystemHull].max >> 1;
}

// The away mission's results (the away mission script sets DS:b256,
// d1ce, b706 before beaming up)
void StarTrekEngine::setAwayMissionResult(int16 score, int16 maxScore, int16 endMethod) {
	_awayMissionScore = score;
	_awayMissionMaxScore = maxScore;
	_missionEndFlag = endMethod;
}

// ---- The missions' handlers ----

void StarTrekEngine::runBridgeHandler() {
	if (_bridgeActions.empty())
		return;
	const BridgeAction ev = _bridgeActions.front();
	const bool tick = ev.type == kBridgeActionTick;
	const bool menu = ev.type == kBridgeActionMenu;
	const bool text = ev.type == kBridgeActionText;
	const bool beamedUp = ev.type == kBridgeActionBeamedUp;
	const uint32 t = _bridgeTicks;
	int16 *f = _missionFlags;
	R3 &ship0 = _enemyShips[0];

	switch (_bridgeHandler) {
	// -- Chapter 1: Demon world (demon), 3c13 --

	case kBridgeHandlerDemonStart: // 3c13:0006
		if (!tick) {
			swallowBridgeAction();
			return;
		}
		if (t == 1) {
			_sound->loadMusicFile("bridgew");
			resetEnterpriseSystems();
			setMission("DEMON");
			missionFlyby("DEM0\\FLYBY", "demon");
		} else if (t == 0x97) {
			bridgeText(kBridgeTalkerCaptainsLog, 0);
		} else if (t == 0xa5) {
			bridgeText(kBridgeTalkerUhura, 1);
			bridgeText(kBridgeTalkerSpock, 2);
			// the Republic, a ship of the Enterprise's class
			spawnShip("ENTERPRI");
			_bridgeHandler = kBridgeHandlerMockBattle;
			_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
			_sound->playSoundEffectIndex(kSfxRedAlert);
		}
		bridgeDefaultAction();
		return;

	case kBridgeHandlerMockBattle: // 3c13:0181
		if (hullBelowHalf(ship0)) {
			// won
			_sound->playMidiMusicTracks(MIDITRACK_3);
			bridgeText(kBridgeTalkerSpock, 4);
			bridgeText(kBridgeTalkerUhura, 5);
		} else if (!hullBelowHalf(_enterpriseR3)) {
			if (menu && ev.data == kBridgeSpock) {
				swallowBridgeAction();
				bridgeText(kBridgeTalkerSpock, 0xb);
			} else if (menu && ev.data == kBridgeUhura) {
				swallowBridgeAction();
				bridgeText(kBridgeTalkerUhura, 0xa);
			} else {
				bridgeDefaultAction();
			}
			return;
		} else {
			// lost
			_sound->playMidiMusicTracks((MidiTracks)4);
			bridgeText(kBridgeTalkerSpock, 6);
			bridgeText(kBridgeTalkerScotty, 7);
			bridgeText(kBridgeTalkerKirk, 8);
			bridgeText(kBridgeTalkerUhura, 9);
		}
		// either way, the Enterprise mended and the Republic away
		removeShots();
		resetEnterpriseSystems();
		bridgeText(kBridgeTalkerSulu, "#BRID\\B_336#Lowering shields and disarming weapons.", 2);
		disableEnemyWeapons(&ship0, true);
		ship0.ship.leaving = 1;
		setBridgeHandler(kBridgeHandlerDemonDebrief);
		return;

	case kBridgeHandlerDemonDebrief: // 3c13:03b1
		if (tick && t == 0x14) {
			_sound->playSoundEffectIndex(kSfxHailing);
			bridgeText(kBridgeTalkerUhura, 0xd);
			bridgeText(kBridgeTalkerKirk, 0xe, 1);
		}
		if (menu) {
			swallowBridgeAction();
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			if (ship0.field1e == 2) {
				delR3(&ship0);
				ship0.field1e = 0;
			}
			viewscreenMessage("woman", "Admiral", 0xc);
			setBridgeHandler(kBridgeHandlerDemonPollux);
			_targetPlanet = kPlanetPollux;
		} else if (text && ev.data == 2) {
			swallowBridgeAction();
			_enterpriseState.shields = _enterpriseState.weapons = false;
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerDemonPollux: // 3c13:0512
		if (menu && ev.data == kBridgeSpock && !_inCombat) {
			swallowBridgeAction();
			if (_currentPlanet != _targetPlanet)
				bridgeText(kBridgeTalkerSpock, 0x14);
			else if (!_enterpriseState.inOrbit)
				bridgeText(kBridgeTalkerSpock, 0x15);
			else
				bridgeText(kBridgeTalkerSpock, 0x16);
		} else if (menu && ev.data == kBridgeUhura && !_inCombat) {
			swallowBridgeAction();
			if (_currentPlanet != _targetPlanet) {
				bridgeText(kBridgeTalkerUhura, 0x10);
			} else if (f[0] == 0) {
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0x11);
				viewscreenMessage("prst", "Priest", 0x12);
				f[0] = 1;
			} else {
				bridgeText(kBridgeTalkerUhura, 0x13);
			}
		} else if (menu && ev.data == kBridgeChekovNavigation) {
			bool wasThere = _currentPlanet == _targetPlanet;
			bridgeDefaultAction();
			if (_currentPlanet == _targetPlanet && !wasThere)
				bridgeText(kBridgeTalkerSpock, 0xf);
		} else if (beamedUp) {
			swallowBridgeAction();
			setBridgeHandler(kBridgeHandlerDemonEnd);
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerDemonEnd: // 3c13:07b5
		if (tick) {
			if (t == 1) {
				_bridgeInputDisabled = true;
				standingCrew(true);
			} else if (t == 10) {
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0x17);
				bridgeText(kBridgeTalkerKirk, 0x18, 1);
			} else if (t == 0x208) {
				bridgeText(kBridgeTalkerMcCoy, 0x19);
				bridgeText(kBridgeTalkerKirk, 0x1a);
				bridgeText(kBridgeTalkerSpock, 0x1b);
				bridgeText(kBridgeTalkerMcCoy, 0x1c, 2);
			}
			bridgeDefaultAction();
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			showMissionReview(0, 0x1d, _awayMissionScore, _awayMissionMaxScore);
			_bridgeTicks = 500;
		} else if (text && ev.data == 2) {
			swallowBridgeAction();
			setOrbit(false, false);
			setBridgeHandler(kBridgeHandlerTugStart);
			standingCrew(false);
			_bridgeInputDisabled = false;
			clearBridgeActions();
		} else {
			swallowBridgeAction();
		}
		return;

	// -- Chapter 2: Hijacked (tug), 3cb2. Its ticks are counted from 0. --

	case kBridgeHandlerTugStart: // 3cb2:0006
		_targetPlanet = kPlanetBetaMyamid;
		if (_currentPlanet == _targetPlanet) {
			setBridgeHandler(kBridgeHandlerTugPirate);
			return;
		}
		if (tick) {
			if (t == 2) {
				_sound->loadMusicFile("bridge");
				setAwayMissionResult(0, 0x20, 0);
				resetEnterpriseSystems();
				setMission("TUG");
				missionFlyby("TUG0\\FLYBY", "hijacked");
			} else if (t == 0x97) {
				_sound->playMidiMusicTracks(MIDITRACK_0);
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0);
				bridgeText(kBridgeTalkerKirk, 1);
			} else if (t == 0xab) {
				viewscreenMessage("woman", "Admiral", 2);
			} else if (t == 0xb0) {
				bridgeText(kBridgeTalkerChekov, 3);
			}
		} else if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 5);
			return;
		} else if (menu && ev.data == kBridgeUhura) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerUhura, 4);
			return;
		}
		bridgeDefaultAction();
		return;

	case kBridgeHandlerTugPirate: // 3cb2:0254, Beta Myamid: the Elasi pirate
		if (tick) {
			if (t == 0x21) {
				bridgeText(kBridgeTalkerSpock, 6);
			} else if (t == 0x41) {
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 7);
				bridgeText(kBridgeTalkerKirk, 8);
				viewscreenTalker("pira");
				viewscreenTalkerText("Elasi Captain", 9);
				bridgeText(kBridgeTalkerKirk, 10);
				restartViewscreenTalker();
				viewscreenTalkerText("Elasi Captain", 11);
				removeViewscreenTalker();
				_sound->playSoundEffectIndex(kSfxRedAlert);
			} else if (t == 0x81) {
				spawnShip("ORION");
				_inCombat = true;
				_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
			}
		} else if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 0xd);
			return;
		} else if (menu && ev.data == kBridgeUhura) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerUhura, 0xc);
			return;
		} else if (menu && ev.data == kBridgeSuluOrbit) {
			swallowBridgeAction();
			return;
		}
		if (_inCombat && ship0.field1e == 2 && hullBelowHalf(ship0)) {
			// it flees
			_sound->playMidiMusicTracks(MIDITRACK_3);
			bridgeText(kBridgeTalkerChekov, 0xe);
			bridgeText(kBridgeTalkerSpock, 0xf);
			disableEnemyWeapons(&ship0, true);
			ship0.ship.leaving = 1;
			setBridgeHandler(kBridgeHandlerTugAfterBattle);
		}
		bridgeDefaultAction();
		return;

	case kBridgeHandlerTugAfterBattle: // 3cb2:0540
		if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 0x11);
		} else if (menu && ev.data == kBridgeUhura) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerUhura, 0x10);
		} else if (menu && ev.data == kBridgeSuluOrbit) {
			swallowBridgeAction();
			if (ship0.field1e != 0) {
				delR3(&ship0);
				ship0.field1e = 0;
			}
			approachObject(6); // the Masada
			setBridgeHandler(kBridgeHandlerTugMasada);
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerTugMasada: { // 3cb2:065d
		if (tick && t == 0x11)
			showTextbox("Mr. Spock", _resource->getLoadedText(0x14), 160, 190, 44, 0);
		if (menu && ev.data == kBridgeKirkTransporter && f[1] == 0) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSulu, 0x1c);
		} else if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 0x1b);
			f[2] = 1;
		} else if (menu && ev.data == kBridgeUhura) {
			swallowBridgeAction();
			static const int16 kHailOrCode[] = { 0x12, 0x13 };
			int choice = f[2] == 1 ? chooseBridgeOption(kHailOrCode, 2) : 0;
			if (choice == 0) {
				bridgeText(kBridgeTalkerUhura, 0x15);
				if (f[3] == 0) {
					viewscreenTalker("pira");
					viewscreenTalkerText("Elasi Cereth", 0x41);
					negotiateWithElasiCereth();
					removeViewscreenTalker();
					f[3] = 1;
				} else {
					viewscreenTalker("pira");
					viewscreenTalkerText("Elasi Cereth", 0x16);
					bridgeText(kBridgeTalkerKirk, 0x17);
					removeViewscreenTalker();
					f[0]++;
					bridgeText(kBridgeTalkerSpock, 0x3c);
				}
				if (f[0] > 4) {
					_missionEndFlag = 2;
					setBridgeHandler(kBridgeHandlerTugEnd);
				}
			} else {
				Common::String code = showCodeInputBox();
				if (code == "293391-197736-3829") {
					bridgeText(kBridgeTalkerUhura, 0x19);
					f[1] = 1;
				} else {
					bridgeText(kBridgeTalkerUhura, 0x1a);
				}
			}
		} else if (beamedUp) {
			setBridgeHandler(kBridgeHandlerTugEnd);
		} else {
			bridgeDefaultAction();
		}
		return;
	}

	case kBridgeHandlerTugEnd: // 3cb2:0954
		if (!tick) {
			swallowBridgeAction();
			return;
		}
		if (t == 1) {
			_bridgeInputDisabled = true;
			if (_missionEndFlag != 2) {
				standingCrew(true);
				if (_missionEndFlag == 1)
					deleteApproachObject();
			}
		} else if (t == 0x14) {
			_sound->playSoundEffectIndex(kSfxHailing);
			if (_missionEndFlag == 0) {
				bridgeText(kBridgeTalkerUhura, 0x1d);
				bridgeText(kBridgeTalkerKirk, 0x1e);
				showMissionReview(1, 0x1f, _awayMissionScore, _awayMissionMaxScore);
				bridgeText(kBridgeTalkerMcCoy, 0x20);
				bridgeText(kBridgeTalkerSpock, 0x21);
				bridgeText(kBridgeTalkerMcCoy, 0x22);
				bridgeText(kBridgeTalkerKirk, 0x23);
				bridgeText(kBridgeTalkerSpock, 0x24);
				bridgeText(kBridgeTalkerKirk, 0x25);
			} else if (_missionEndFlag == 1) {
				bridgeText(kBridgeTalkerUhura, 0x27);
				bridgeText(kBridgeTalkerKirk, 0x28);
				showMissionReview(1, 0x29, 0, 0);
			} else {
				bridgeText(kBridgeTalkerUhura, 0x2a);
				showMissionReview(1, 0x2b, 0, 0);
			}
			deleteApproachObject();
			setOrbit(false, false);
			standingCrew(false);
			setBridgeHandler(kBridgeHandlerLoveStart);
			_bridgeInputDisabled = false;
			clearBridgeActions();
			return;
		}
		bridgeDefaultAction();
		return;

	// -- Chapter 3: Love's Labor Jeopardized (love), 3ddd --

	case kBridgeHandlerLoveStart: // 3ddd:000a
		if (tick) {
			if (t == 1) {
				_sound->loadMusicFile("bridge");
				resetEnterpriseSystems();
				setMission("LOVE");
				missionFlyby("LOV0\\FLYBY", "tlove");
			} else if (t == 0x97) {
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0);
				bridgeText(kBridgeTalkerKirk, 1, 1);
			}
			bridgeDefaultAction();
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			viewscreenMessage("woman", "Admiral", 2);
			setBridgeHandler(kBridgeHandlerLoveEnRoute);
			_targetPlanet = kPlanetArk7;
		} else {
			swallowBridgeAction();
		}
		return;

	case kBridgeHandlerLoveEnRoute: // 3ddd:017f
		if (!_inCombat && _sound->getLoopingMidiTrack() == MIDITRACK_2)
			_sound->playMidiMusicTracks(MIDITRACK_NONE);
		if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			if (f[0] == 0)
				bridgeText(kBridgeTalkerSpock, 4);
		} else if (menu && ev.data == kBridgeUhura && !_inCombat) {
			swallowBridgeAction();
			if (f[0] == 0)
				bridgeText(kBridgeTalkerUhura, 5);
		} else if (menu && ev.data == kBridgeChekovNavigation) {
			bool wasThere = _currentPlanet == _targetPlanet;
			bridgeDefaultAction();
			if (_currentPlanet == _targetPlanet && !wasThere) {
				bridgeText(kBridgeTalkerSpock, 3, 1);
				f[0] = 1;
			}
		} else if (menu && ev.data != kBridgeUhura) {
			if (f[0] == 0)
				bridgeDefaultAction();
			else
				swallowBridgeAction();
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			setBridgeHandler(kBridgeHandlerLoveRomulan);
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerLoveRomulan: // 3ddd:033e
		if (tick) {
			if (t == 1) {
				R3 *romulan = spawnShip("ROMULAN");
				if (romulan)
					romulan->ship.systems[kSystemWeapon0].enabled = 0;
				showTextboxBridge(kBridgeTalkerSpock, "#BRID\\B_160#Romulan ship decloaking, Captain.");
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 6, 1);
			} else if (t > 1 && !_inCombat) {
				setBridgeHandler(kBridgeHandlerLoveAfterBattle);
			}
			bridgeDefaultAction();
		} else if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			if (f[0] == 0)
				bridgeText(kBridgeTalkerSpock, 8);
		} else if (menu && ev.data == kBridgeUhura) {
			swallowBridgeAction();
			if (f[0] == 0)
				bridgeText(kBridgeTalkerUhura, 7);
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			f[0] = 0;
			viewscreenMessage("romula", "Romulan Captain", 9);
			bridgeText(kBridgeTalkerSpock, 0xa);
			ship0.ship.systems[kSystemWeapon0].enabled = 1;
			_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
			_sound->playSoundEffectIndex(kSfxRedAlert);
		} else if (f[0] == 0) {
			bridgeDefaultAction();
		} else {
			swallowBridgeAction();
		}
		return;

	case kBridgeHandlerLoveAfterBattle: // 3ddd:057b
		if (tick && t == 1) {
			bridgeText(kBridgeTalkerSpock, 0xb);
			bridgeText(kBridgeTalkerSulu, 0xc, 1);
			f[0] = 1;
		}
		if (text) {
			swallowBridgeAction();
			if (ev.data == 1) {
				approachObject(4); // ARK7
				setBridgeHandler(kBridgeHandlerLoveArk7);
				f[0] = 0;
			}
		} else if (f[0] == 0 || tick) {
			bridgeDefaultAction();
		} else {
			swallowBridgeAction();
		}
		return;

	case kBridgeHandlerLoveArk7: // 3ddd:0692
		if (tick && t == 10) {
			bridgeText(kBridgeTalkerUhura, 0x11);
			postBridgeAction(kBridgeActionText, 2);
		}
		if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 0xd);
		} else if (menu && ev.data == kBridgeUhura && f[1] == 0) {
			swallowBridgeAction();
			_sound->playSoundEffectIndex(kSfxHailing);
			bridgeText(kBridgeTalkerUhura, 0xe, 1);
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			viewscreenMessage("romula", "Centurion Preax", 0xf);
			f[1] = 1;
		} else if (text && ev.data == 2) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerAutobeacon, 0x10);
		} else if (beamedUp) {
			swallowBridgeAction();
			setBridgeHandler(kBridgeHandlerLoveEnd);
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerLoveEnd: // 3ddd:086b
		if (tick) {
			if (t == 1) {
				_bridgeInputDisabled = true;
				standingCrew(true);
			} else if (t == 10) {
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0x12);
				bridgeText(kBridgeTalkerKirk, 0x13, 1);
			} else if (t == 0x208) {
				bridgeText(kBridgeTalkerKirk, 0x14);
				bridgeText(kBridgeTalkerMcCoy, 0x15);
				bridgeText(kBridgeTalkerKirk, 0x16);
				bridgeText(kBridgeTalkerMcCoy, 0x17);
				bridgeText(kBridgeTalkerScotty, 0x18);
				bridgeText(kBridgeTalkerKirk, 0x19, 2);
			}
			bridgeDefaultAction();
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			showMissionReview(2, 0x1a, _awayMissionScore, _awayMissionMaxScore);
			_bridgeTicks = 500;
		} else if (text && ev.data == 2) {
			swallowBridgeAction();
			deleteApproachObject();
			setOrbit(false, false);
			setBridgeHandler(kBridgeHandlerMuddStart);
			standingCrew(false);
			_bridgeInputDisabled = false;
			clearBridgeActions();
		} else {
			swallowBridgeAction();
		}
		return;

	// -- Chapter 4: Another Fine Mess (mudd), 3e8d --

	case kBridgeHandlerMuddStart: // 3e8d:0004
		if (f[0] == 0 && !tick && !text) {
			swallowBridgeAction();
			return;
		}
		if (tick) {
			if (t == 1) {
				_sound->loadMusicFile("bridge");
				resetEnterpriseSystems();
				setMission("MUDD");
				missionFlyby("MUD0\\FLYBY", "another");
			} else if (t == 0x97) {
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0);
				bridgeText(kBridgeTalkerKirk, 1, 1);
			}
			bridgeDefaultAction();
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			viewscreenMessage("woman", "Admiral", 2);
			f[0] = 1;
			_targetPlanet = kPlanetHarlequin;
		} else if (menu && ev.data == kBridgeUhura && f[3] == 0) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerUhura, 3);
		} else if (menu && ev.data == kBridgeSpock && f[3] == 0) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 4);
		} else if (menu && ev.data == kBridgeChekovNavigation) {
			f[3] = 1;
			bridgeDefaultAction();
			if (_currentPlanet == kPlanetHarlequin) {
				bridgeText(kBridgeTalkerSpock, 5);
				setBridgeHandler(kBridgeHandlerMuddHarlequin);
			}
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerMuddHarlequin: // 3e8d:02d4: two Elasi ships, Mudd's call
		if (tick) {
			if (t == 10) {
				f[1] = 0;
				bridgeText(kBridgeTalkerSpock, 6);
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 7, 1);
			} else {
				if (f[1] && !_inCombat && !f[2]) {
					// both pirates gone
					_sound->playMidiMusicTracks(MIDITRACK_NONE);
					bridgeText(kBridgeTalkerSpock, 8);
					bridgeText(kBridgeTalkerUhura, 9);
					bridgeText(kBridgeTalkerKirk, 0xa);
					showTextbox("Voice", _resource->getLoadedText(0xb), 160, 130, 44, 0);
					bridgeText(kBridgeTalkerKirk, 0xc);
					bridgeText(kBridgeTalkerUhura, 0xd, 2);
					f[2] = 1;
					_targetPlanet = kPlanetHarrapa;
				}
				if (_inCombat) {
					for (int i = 0; i < 2; i++) {
						ShipState &s = _enemyShips[i].ship;
						if (_enemyShips[i].field1e != 0 && 2 * s.systems[kSystemHull].cur <= s.systems[kSystemHull].max)
							s.leaving = 1;
					}
				}
			}
			bridgeDefaultAction();
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			viewscreenMessage("pira", "Elasi Pirate", 0xe);
			spawnShip("ORION");
			spawnShip("ORION");
			f[1] = 1;
			_inCombat = true;
			_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
		} else if (text && ev.data == 2) {
			swallowBridgeAction();
			viewscreenTalker("mudd");
			viewscreenTalkerText("Harry Mudd", 0xf);
			bridgeText(kBridgeTalkerKirk, 0x10);
			restartViewscreenTalker();
			viewscreenTalkerText("Harry Mudd", 0x11);
			removeViewscreenTalker();
			bridgeText(kBridgeTalkerSpock, 0x12);
		} else if (menu && ev.data == kBridgeUhura && (_inCombat || f[2])) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerUhura, _inCombat ? 0x13 : 0x14);
		} else if (menu && ev.data == kBridgeSpock && (_inCombat || f[2])) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, _inCombat ? 0x15 : 0x16);
		} else if (menu && ev.data == kBridgeChekovNavigation) {
			if (f[2] == 0) {
				swallowBridgeAction();
			} else {
				bridgeDefaultAction();
				if (_currentPlanet == _targetPlanet) {
					approachObject(5); // Mudd's derelict
					setBridgeHandler(kBridgeHandlerMuddHarrapa);
				}
			}
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerMuddHarrapa: // 3e8d:07bb
		if (tick) {
			if (t == 100) {
				bridgeText(kBridgeTalkerSpock, 0x17);
				bridgeText(kBridgeTalkerKirk, 0x18);
				bridgeText(kBridgeTalkerSpock, 0x19);
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0x1a);
				viewscreenTalker("mudd");
				viewscreenTalkerText("Harry Mudd", 0x1b);
				bridgeText(kBridgeTalkerKirk, 0x1c);
				restartViewscreenTalker();
				viewscreenTalkerText("Harry Mudd", 0x1d);
				bridgeText(kBridgeTalkerSpock, 0x1e);
				restartViewscreenTalker();
				viewscreenTalkerText("Harry Mudd", 0x1f);
				bridgeText(kBridgeTalkerKirk, 0x20);
				restartViewscreenTalker();
				viewscreenTalkerText("Harry Mudd", 0x21);
				bridgeText(kBridgeTalkerKirk, 0x22);
				removeViewscreenTalker();
			}
			if (f[5] != 0) {
				if (f[5] == 1) {
					showMissionReview(3, 0x23, _awayMissionScore, _awayMissionMaxScore);
					if (_missionEndFlag == 0) {
						bridgeText(kBridgeTalkerKirk, 0x26);
					} else {
						bridgeText(kBridgeTalkerScotty, 0x24);
						bridgeText(kBridgeTalkerKirk, 0x25);
					}
					bridgeText(kBridgeTalkerUhura, 0x29);
					bridgeText(kBridgeTalkerKirk, 0x2a);
					bridgeText(kBridgeTalkerUhura, 0x2b);
					bridgeText(kBridgeTalkerKirk, 0x2c, 4);
				}
				f[5]--;
			}
			bridgeDefaultAction();
		} else if (menu) {
			if (f[4]) {
				swallowBridgeAction();
			} else if (ev.data == kBridgeUhura || ev.data == kBridgeChekovNavigation) {
				swallowBridgeAction();
				bridgeText(kBridgeTalkerUhura, 0x2d);
			} else if (ev.data == kBridgeSpock) {
				swallowBridgeAction();
				bridgeText(kBridgeTalkerSpock, 0x2e);
			} else if (ev.data == kBridgeSuluOrbit) {
				swallowBridgeAction();
			} else {
				bridgeDefaultAction();
			}
		} else if (beamedUp) {
			f[4] = 1;
			f[5] = 10;
			standingCrew(true);
			_bridgeInputDisabled = true;
			if (_bridgeTicks < 0x65)
				_bridgeTicks = 0x65;
			swallowBridgeAction();
		} else if (text && ev.data == 4) {
			deleteApproachObject();
			setOrbit(false, false);
			setBridgeHandler(kBridgeHandlerFeatherStart);
			swallowBridgeAction();
			standingCrew(false);
			_bridgeInputDisabled = false;
			clearBridgeActions();
		} else {
			bridgeDefaultAction();
		}
		return;

	// -- Chapter 5A: The Feathered Serpent (feather), 3f59 --

	case kBridgeHandlerFeatherStart: // 3f59:000a: Digifal, the Klingon Taraz
		if (!tick) {
			if (menu && (ev.data == kBridgeUnk1 || ev.data == kBridgeUnk2)) {
				// Kirk fires on the Klingon
				if (f[0] == 0 && _inCombat && _enterpriseR3.ship.weaponsArmed) {
					f[0] = 1;
					viewscreenMessage("klg1", "Taraz", 0xc);
					enableEnemyWeapons(&ship0);
				}
				bridgeDefaultAction();
			} else if (menu && ev.data == kBridgeUhura) {
				swallowBridgeAction();
				bridgeText(kBridgeTalkerUhura, 0xd);
			} else if (menu && ev.data == kBridgeSuluOrbit && f[0] == 0 && _currentPlanet == _targetPlanet) {
				swallowBridgeAction();
				setBridgeHandler(kBridgeHandlerFeatherOrbit);
			} else if (menu && ev.data == kBridgeChekovNavigation) {
				if (_currentPlanet == _targetPlanet) {
					if (f[0] == 0)
						_inCombat = false;
					bridgeDefaultAction();
				} else {
					bridgeDefaultAction();
					if (_currentPlanet == _targetPlanet) {
						f[1] = 1;
						f[2] = 0x1e;
					}
				}
			} else {
				bridgeDefaultAction();
			}
			return;
		}
		if (t == 1) {
			_sound->loadMusicFile("bridgeb");
			resetEnterpriseSystems();
			setMission("FEATHER");
			_targetPlanet = kPlanetDigifal;
			missionFlyby("FEA0\\FLYBY", "feather");
		} else if (t == 0x97) {
			bridgeText(kBridgeTalkerCaptainsLog, 5);
		}
		if (f[0] == 1 && ship0.field1e == 2) {
			// fighting the Klingons: one that's hurt goes, another comes
			int sumMax = 0, sumCur = 0;
			for (int i = 0; i < 6; i++) {
				sumMax += ship0.ship.shields[i].max;
				sumCur += ship0.ship.shields[i].cur;
			}
			if (sumCur * 3 < sumMax * 2) {
				ship0.ship.leaving = 1;
				spawnShip("KLINGON");
			}
		}
		if (f[1] && f[0] == 0) {
			if (f[2] != 0) {
				f[2]--;
			} else {
				f[1] = 0;
				R3 *klingon = spawnShip("KLINGON");
				if (klingon) {
					klingon->ship.systems[kSystemWeapon0].enabled = 0;
					klingon->ship.systems[kSystemWeapon0 + 2].enabled = 0;
					klingon->ship.systems[kSystemWeapon0 + 3].enabled = 0;
				}
				renderSpace();
				bridgeText(kBridgeTalkerUhura, 6);
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerKirk, 7);
				viewscreenTalker("klg1");
				viewscreenTalkerText("Taraz", 8);
				static const int16 kTaraz[] = { 0, 1, 2 };
				int c = chooseBridgeOption(kTaraz, 3);
				bool fight = true;
				if (c == 1) {
					viewscreenTalkerText("Taraz", 0xb);
					static const int16 kTaraz2[] = { 3, 4 };
					if (chooseBridgeOption(kTaraz2, 2) != 1) {
						// agreed: peace
						removeViewscreenTalker();
						fight = false;
					}
				}
				if (fight) {
					viewscreenTalkerText("Taraz", 9);
					removeViewscreenTalker();
					_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
					_sound->playSoundEffectIndex(kSfxRedAlert);
					bridgeText(kBridgeTalkerSpock, 0xa);
					f[0] = 1;
					enableEnemyWeapons(&ship0);
				}
			}
		}
		bridgeDefaultAction();
		return;

	case kBridgeHandlerFeatherOrbit: // 3f59:049e: Digifal, Admiral Kenka
		_inCombat = false;
		if (tick && t == 1)
			setOrbit(true, true);
		if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 0x14);
		} else if (menu && ev.data == kBridgeSuluOrbit) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSulu, 0x23);
		} else if (beamedUp) {
			swallowBridgeAction();
			setOrbit(false, false);
			bridgeText(kBridgeTalkerUhura, 0x15);
			_sound->playSoundEffectIndex(kSfxHailing);
			bridgeText(kBridgeTalkerUhura, 0x16);
			postBridgeAction(kBridgeActionText, 1);
		} else if (text && ev.data == 1) {
			swallowBridgeAction();
			viewscreenTalker("klg2");
			viewscreenTalkerText("Admiral Kenka", 0x17);
			static const int16 kKenka1[] = { 0xe, 0xf, 0x10 };
			int c = chooseBridgeOption(kKenka1, 3);
			if (c <= 1) {
				viewscreenTalkerText("Admiral Kenka", 0x18);
				bridgeText(kBridgeTalkerKirk, 0x19);
				viewscreenTalkerText("Admiral Kenka", 0x1a);
				static const int16 kKenka2[] = { 0x11, 0x12, 0x13 };
				c = chooseBridgeOption(kKenka2, 3);
				if (c <= 1) {
					// he'll wait: to Hrakkour
					viewscreenTalkerText("Admiral Kenka", 0x1b);
					_sound->playSoundEffectIndex(kSfxHailing);
					bridgeText(kBridgeTalkerUhura, 0x1c);
					bridgeText(kBridgeTalkerKirk, 0x1d);
					viewscreenTalkerText("Admiral Kenka", 0x1e);
					removeViewscreenTalker();
					viewscreenMessage("woman", "Admiral", 0x1f);
					bridgeText(kBridgeTalkerScotty, 0x20);
					bridgeText(kBridgeTalkerKirk, 0x21);
					_enterpriseState.inOrbit = false;
					if (ship0.field1e == 2) {
						delR3(&ship0);
						ship0.field1e = 0;
					}
					_currentPlanet = _targetPlanet = kPlanetHrakkour;
					showFullScreenSpace();
					setStarSystemPlanet(kPlanetHrakkour);
					warpSequence();
					_gfx->loadPalette("bridge");
					showBridgeViewscreen();
					renderSpace();
					_gfx->fadeinScreen();
					setBridgeHandler(kBridgeHandlerTrialStart);
					clearBridgeActions();
					return;
				}
			}
			// insulted: the K'lrta, with no way out
			removeViewscreenTalker();
			bridgeText(kBridgeTalkerSulu, 0x22);
			_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
			_sound->playSoundEffectIndex(kSfxRedAlert);
			spawnShip("K'LRTA");
			setBridgeHandler(kBridgeHandlerFeatherKlrta);
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerFeatherKlrta: // 3f59:0931
		spawnShip("K'LRTA");
		bridgeDefaultAction();
		return;

	// -- Chapter 5B: The Feathered Serpent (trial), 3fed --

	case kBridgeHandlerTrialStart: // 3fed:000c
		if (tick) {
			if (t == 1) {
				_currentPlanet = _targetPlanet = kPlanetHrakkour;
				setMission("TRIAL");
				// (feather's score, to count with trial's)
				f[1] = _awayMissionScore;
				f[2] = _awayMissionMaxScore;
				setOrbit(true, false);
			} else if (t == 0x32) {
				bridgeText(kBridgeTalkerCaptainsLog, 0);
				postBridgeAction(kBridgeActionMenu, kBridgeKirkTransporter);
			}
			bridgeDefaultAction();
		} else if (menu && ev.data == kBridgeKirkTransporter) {
			bridgeDefaultAction();
		} else if (beamedUp) {
			swallowBridgeAction();
			setOrbit(false, false);
			switch (_missionEndFlag) {
			case 0:
				setBridgeHandler(kBridgeHandlerTrialEnd0);
				break;
			case 1:
			case 2:
				setBridgeHandler(kBridgeHandlerTrialEnd1);
				break;
			case 3:
				setBridgeHandler(kBridgeHandlerTrialEnd3);
				break;
			default:
				setBridgeHandler(kBridgeHandlerTrialEnd4);
				break;
			}
		} else {
			swallowBridgeAction();
		}
		return;

	case kBridgeHandlerTrialEnd0: // 3fed:01e5
	case kBridgeHandlerTrialEnd1: // 3fed:02f1
	case kBridgeHandlerTrialEnd3: // 3fed:041a
		if (!tick) {
			swallowBridgeAction();
			return;
		}
		if (t == 0x14) {
			_bridgeInputDisabled = true;
			if (_bridgeHandler == kBridgeHandlerTrialEnd0) {
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 1);
				viewscreenMessage("klg2", "Vlict Kenka", 2);
				bridgeText(kBridgeTalkerUhura, 3);
				bridgeText(kBridgeTalkerKirk, 4);
			} else if (_bridgeHandler == kBridgeHandlerTrialEnd1) {
				bridgeText(kBridgeTalkerKirk, 5);
				bridgeText(kBridgeTalkerSpock, 6);
				bridgeText(kBridgeTalkerKirk, 7);
				bridgeText(kBridgeTalkerSpock, 8);
				bridgeText(kBridgeTalkerKirk, 9);
			} else {
				bridgeText(kBridgeTalkerSulu, 0xa);
				bridgeText(kBridgeTalkerKirk, 0xb);
				bridgeText(kBridgeTalkerSpock, 0xc);
				bridgeText(kBridgeTalkerKirk, 0xd);
			}
			swallowBridgeAction();
			endTrialMission();
			return;
		}
		bridgeDefaultAction();
		return;

	case kBridgeHandlerTrialEnd4: // 3fed:0521: the good end, Kallarax
		if (tick && t == 0x14) {
			_bridgeInputDisabled = true;
			_sound->playSoundEffectIndex(kSfxHailing);
			bridgeText(kBridgeTalkerUhura, 0x11);
			viewscreenTalker("klg1");
			viewscreenTalkerText("Kallarax", 0x12);
			static const int16 kKallarax[] = { 0xe, 0xf, 0x10 };
			if (chooseBridgeOption(kKallarax, 3) <= 1) {
				viewscreenTalkerText("Kallarax", 0x13);
				removeViewscreenTalker();
				bridgeText(kBridgeTalkerUhura, 0x14);
				bridgeText(kBridgeTalkerKirk, 0x15);
				swallowBridgeAction();
				endTrialMission();
				return;
			}
			setBridgeHandler(kBridgeHandlerTrialKlrta);
		}
		bridgeDefaultAction();
		return;

	case kBridgeHandlerTrialKlrta: // 3fed:06ba: the K'lrta after a remark
		if (tick && t == 1) {
			viewscreenTalkerText("Kallarax", 0x16);
			removeViewscreenTalker();
			_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
			_sound->playSoundEffectIndex(kSfxRedAlert);
			bridgeText(kBridgeTalkerSpock, 0x17);
			spawnShip("K'LRTA");
			_inCombat = true;
			_bridgeInputDisabled = false;
		} else if (tick && t > 2 && !_inCombat) {
			_bridgeInputDisabled = true;
			_sound->playMidiMusicTracks(MIDITRACK_NONE);
			bridgeText(kBridgeTalkerKirk, 0x18);
			bridgeText(kBridgeTalkerSpock, 0x19);
			swallowBridgeAction();
			endTrialMission();
			return;
		}
		bridgeDefaultAction();
		return;

	// -- Chapter 6: That Old Devil Moon (sins), 4074 --

	case kBridgeHandlerSinsStart: // 4074:000e
		if (tick) {
			if (t == 1) {
				_sound->loadMusicFile("bridge");
				resetEnterpriseSystems();
				setMission("SINS");
				missionFlyby("SIN0\\FLYBY", "devilmon");
			} else if (t == 0x97) {
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0);
				bridgeText(kBridgeTalkerKirk, 1);
				viewscreenMessage("woman", "Admiral", 2);
				bridgeText(kBridgeTalkerSpock, 3);
				bridgeText(kBridgeTalkerKirk, 4);
				bridgeText(kBridgeTalkerSpock, 5);
				bridgeText(kBridgeTalkerChekov, 6);
				bridgeText(kBridgeTalkerSpock, 7);
				bridgeText(kBridgeTalkerKirk, 8);
				bridgeText(kBridgeTalkerSpock, 9);
				_targetPlanet = kPlanetAlphaProxima;
			}
			bridgeDefaultAction();
		} else if (menu && ev.data == kBridgeUhura) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerUhura, 0xa);
		} else if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 0xb);
		} else if (menu && ev.data == kBridgeChekovNavigation) {
			bridgeDefaultAction();
			if (_currentPlanet == _targetPlanet)
				setBridgeHandler(kBridgeHandlerSinsEnd);
		} else {
			bridgeDefaultAction();
		}
		return;

	case kBridgeHandlerSinsEnd: // 4074:0307
		if (menu && ev.data == kBridgeUhura) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerUhura, f[0] == 0 ? 0xd : 0xc);
		} else if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			if (!_enterpriseState.inOrbit) {
				bridgeText(kBridgeTalkerSpock, 0xe);
			} else if (f[0] == 0) {
				f[0] = 1;
				bridgeText(kBridgeTalkerSpock, 0xf);
				bridgeText(kBridgeTalkerSulu, 0x10);
				bridgeText(kBridgeTalkerKirk, 0x11);
				bridgeText(kBridgeTalkerUhura, 0x12);
				bridgeText(kBridgeTalkerKirk, 0x13);
				bridgeText(kBridgeTalkerSpock, 0x14);
			} else {
				bridgeText(kBridgeTalkerSpock, 0x15);
			}
		} else if (beamedUp) {
			swallowBridgeAction();
			_bridgeInputDisabled = true;
			renderSpace();
			standingCrew(true);
			_sound->playSoundEffectIndex(kSfxHailing);
			bridgeText(kBridgeTalkerUhura, 0x16);
			bridgeText(kBridgeTalkerKirk, 0x17);
			showMissionReview(5, 0x18, _awayMissionScore, _awayMissionMaxScore);
			bridgeText(kBridgeTalkerMcCoy, 0x19);
			bridgeText(kBridgeTalkerSpock, 0x1a);
			bridgeText(kBridgeTalkerMcCoy, 0x1b);
			bridgeText(kBridgeTalkerKirk, 0x1c);
			showTextbox("Captain Kirk", _resource->getLoadedText(0x1d), 160, 100, 44, 0);
			bridgeText(kBridgeTalkerMcCoy, 0x1e);
			bridgeText(kBridgeTalkerKirk, 0x1f);
			setOrbit(false, false);
			standingCrew(false);
			setBridgeHandler(kBridgeHandlerVengStart);
			_bridgeInputDisabled = false;
			clearBridgeActions();
		} else {
			bridgeDefaultAction();
		}
		return;

	// -- Chapter 7: Vengeance (veng), 40e6 --

	case kBridgeHandlerVengStart: { // 40e6:0006: the Republic's wreck, Bredell
		if (tick) {
			if (t == 1) {
				_sound->loadMusicFile("bridgew");
				_targetPlanet = _currentPlanet;
				resetEnterpriseSystems();
				setMission("VENG");
				missionFlyby("VEN0\\FLYBY", "vengence"); // (the original's spelling)
			} else if (t == 0x97) {
				bridgeText(kBridgeTalkerKirk, 0xa);
				bridgeText(kBridgeTalkerSpock, 0xb);
				approachObject(7); // the Republic
				bridgeText(kBridgeTalkerKirk, 0xc);
				bridgeText(kBridgeTalkerUhura, 0xd);
				static const int16 kBeamOver[] = { 0, 1 };
				chooseBridgeOption(kBeamOver, 2);
				_enterpriseState.inOrbit = true;
				postBridgeAction(kBridgeActionMenu, kBridgeKirkTransporter);
			}
			bridgeDefaultAction();
		} else if (menu && ev.data == kBridgeKirkTransporter) {
			bridgeDefaultAction();
		} else if (beamedUp) {
			swallowBridgeAction();
			f[0] = _awayMissionScore;
			f[1] = _awayMissionMaxScore;
			_enterpriseState.inOrbit = false;
			showFullScreenSpace();
			deleteApproachObject();
			warpSequence();
			_gfx->loadPalette("bridge");
			setRandomPlanet();
			showBridgeViewscreen();
			loadActorAnim(2, "xstndmc", 0, 0, 1.0);
			renderSpace();
			_gfx->fadeinScreen();
			_enterpriseR3.ship.requestedSpeed = _enterpriseR3.ship.maxSpeed;
			bridgeText(kBridgeTalkerKirk, 0xe);
			bridgeText(kBridgeTalkerSpock, 0xf);
			bridgeText(kBridgeTalkerKirk, 0x10);
			bridgeText(kBridgeTalkerSpock, 0x11);
			bridgeText(kBridgeTalkerSpock, 0x12);
			bridgeText(kBridgeTalkerUhura, 0x13);
			bridgeText(kBridgeTalkerSpock, 0x14);
			static const int16 kHelp[] = { 2, 3 };
			if (chooseBridgeOption(kHelp, 2) != 0) {
				bridgeText(kBridgeTalkerSulu, 0x20);
				_sound->playSoundEffectIndex(kSfxHailing);
				bridgeText(kBridgeTalkerUhura, 0x21);
			}
			viewscreenTalker("maddoc");
			viewscreenTalkerText("Bredell", 0x15);
			bridgeText(kBridgeTalkerKirk, 0x16);
			viewscreenTalkerText("Bredell", 0x17);
			static const int16 kBredell[] = { 4, 5, 6 };
			switch (chooseBridgeOption(kBredell, 3)) {
			case 0: {
				viewscreenTalkerText("Bredell", 0x18);
				bridgeText(kBridgeTalkerKirk, 0x19);
				viewscreenTalkerText("Bredell", 0x1a);
				bridgeText(kBridgeTalkerKirk, 0x1b);
				viewscreenTalkerText("Bredell", 0x1c);
				static const int16 kBredell2[] = { 7, 8, 9 };
				chooseBridgeOption(kBredell2, 3);
				viewscreenTalkerText("Bredell", 0x1d);
				break;
			}
			case 1:
				viewscreenTalkerText("Bredell", 0x1e);
				break;
			default:
				viewscreenTalkerText("Bredell", 0x1f);
				break;
			}
			spawnShip("VARDAIN");
			_inCombat = true;
			removeViewscreenTalker();
			setBridgeHandler(kBridgeHandlerVengBattle);
			_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
			_sound->playSoundEffectIndex(kSfxRedAlert);
		} else {
			swallowBridgeAction();
		}
		return;
	}

	case kBridgeHandlerVengBattle: // 40e6:05d4: the Vardain, the end of the game
		if (tick && t == 300 && _inCombat) {
			_sound->playSoundEffectIndex(kSfxRedAlert);
			bridgeText(kBridgeTalkerSpock, 0x22);
			spawnShip("3PIRATE");
			spawnShip("ORION");
		}
		if (menu && ev.data == kBridgeSpock) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerSpock, 0x23);
		} else if (menu && ev.data == kBridgeUhura) {
			swallowBridgeAction();
			bridgeText(kBridgeTalkerUhura, 0x24);
		} else if (tick && !_inCombat && t > 2) {
			swallowBridgeAction();
			endOfGame();
		} else {
			bridgeDefaultAction();
		}
		return;

	default:
		bridgeDefaultAction();
		return;
	}
}

// Trial's end (3fed:081a): warp out, the review of both halves
void StarTrekEngine::endTrialMission() {
	showFullScreenSpace();
	warpSequence();
	_gfx->loadPalette("bridge");
	showBridgeViewscreen();
	renderSpace();
	_gfx->fadeinScreen();
	showMissionReview(4, 0x1a, _missionFlags[1] + _awayMissionScore, _missionFlags[2] + _awayMissionMaxScore);
	setBridgeHandler(kBridgeHandlerSinsStart);
	_bridgeInputDisabled = false;
}

// The last battle won (40e6:05d4): the crew, the review, the career
void StarTrekEngine::endOfGame() {
	if (_spaceFullScreen)
		showBridgeViewscreen();
	_bridgeInputDisabled = true;
	bridgeText(kBridgeTalkerSpock, 0x25);
	bridgeText(kBridgeTalkerKirk, 0x26);
	bridgeText(kBridgeTalkerScotty, 0x27);
	bridgeText(kBridgeTalkerChekov, 0x28);
	bridgeText(kBridgeTalkerKirk, 0x29);
	bridgeText(kBridgeTalkerUhura, 0x2a);
	bridgeText(kBridgeTalkerKirk, 0x2b);
	showMissionReview(6, 0x2c, _missionFlags[0], _missionFlags[1], false);

	int total = 0;
	for (int i = 0; i < 7; i++)
		total += _missionPoints[i];
	int rating = CLIP(total * 100 / 28, 0, 100);
	showTextbox("Admiral", Common::String::format(_resource->getLoadedText(0x2d).c_str(),
		getSpeechSampleForNumber(rating).c_str(), rating), 160, 190, 44, 0);

	static const int kEndings[3][8] = {
		{ 0x2e, 0x2f, 0x30, 0x31, 0x32, 0x33, 0x34, 0 },
		{ 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c },
		{ 0x3d, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44 }
	};
	static const int kTalkers[3][7] = {
		{ kBridgeTalkerMcCoy, kBridgeTalkerSpock, kBridgeTalkerKirk, kBridgeTalkerMcCoy, kBridgeTalkerSpock, kBridgeTalkerKirk, -1 },
		{ kBridgeTalkerKirk, kBridgeTalkerSpock, kBridgeTalkerMcCoy, kBridgeTalkerSpock, kBridgeTalkerKirk, kBridgeTalkerMcCoy, kBridgeTalkerKirk },
		{ kBridgeTalkerMcCoy, kBridgeTalkerSpock, kBridgeTalkerMcCoy, kBridgeTalkerKirk, kBridgeTalkerSpock, kBridgeTalkerMcCoy, kBridgeTalkerKirk }
	};
	int band = rating < 51 ? 0 : rating < 86 ? 1 : 2;
	viewscreenTalkerText("Admiral", kEndings[band][0]);
	removeViewscreenTalker();
	for (int i = 0; i < 7 && kTalkers[band][i] >= 0; i++)
		bridgeText(kTalkers[band][i], kEndings[band][i + 1]);

	showFullScreenSpace();
	warpSequence();
	_bridgeInputDisabled = false;
	clearBridgeActions();
	// (then the credits, and the menu: load, restart or quit)
	showGameOverMenu();
}

// ---- Random encounters (11dc:330a, 3499) ----

// Warping to the wrong star: 1 to 4 Klingons, Romulans or Elasi
void StarTrekEngine::startRandomEncounter() {
	_randomEncounterType = combatRand() % 3 + 1;
	_encounterCountdown = 10;
	int n = (combatRand() & 3) + 1;
	for (int i = 0; i < n; i++) {
		switch (_randomEncounterType) {
		case kRandomEncounterKlingon:
			spawnShip("KLINGON");
			break;
		case kRandomEncounterRomulan:
			spawnShip((combatRand() & 1) == 0 ? "CKLING" : "ROMULAN");
			break;
		default: {
			static const char *const kElasi[] = { "ORION", "PIRATE1", "MPIRATE" };
			spawnShip(kElasi[combatRand() % 3]);
			break;
		}
		}
	}
	static const char *const kTexts[] = { "", "klingon", "romulan", "elasi" };
	_resource->setTxtFileName(kTexts[_randomEncounterType]);
	_inCombat = true;
	_enterpriseState.underAttack = true;
	_sound->loadMusicFile(n < 3 ? "bridge" : "bridgeb");
}

void StarTrekEngine::randomEncounterHandler() {
	if (_encounterCountdown > 0) {
		_encounterCountdown--;
		bridgeDefaultAction();
		return;
	}
	if (_encounterCountdown == 0) {
		// the hail (this action stays queued)
		_encounterCountdown = -1;
		bridgeText(kBridgeTalkerChekov, 0);
		bridgeText(kBridgeTalkerSpock, 1);
		static const char *const kNames[] = { "", "Mr. Spock", "Mr. Sulu", "Captain Kirk" };
		showTextbox(kNames[_randomEncounterType], _resource->getLoadedText(2), 122, 116, 176, 0);
		bridgeText(kBridgeTalkerScotty, 3);
		_sound->playSoundEffectIndex(kSfxHailing);
		bridgeText(kBridgeTalkerUhura, 4);
		bridgeText(kBridgeTalkerKirk, 5);
		static const char *const kCaptains[] = { "", "klg1", "romula", "pira" };
		viewscreenMessage(kCaptains[_randomEncounterType], "Captain", 6);
		_sound->playMidiMusicTracks(MIDITRACK_2, kLoopTypeRepeat);
		_sound->playSoundEffectIndex(kSfxRedAlert);
		return;
	}
	const BridgeAction ev = _bridgeActions.front();
	if (ev.type == kBridgeActionMenu && ev.data == kBridgeSpock) {
		swallowBridgeAction();
		bridgeText(kBridgeTalkerSpock, 7);
	} else if (ev.type == kBridgeActionMenu && ev.data == kBridgeUhura) {
		swallowBridgeAction();
		bridgeText(kBridgeTalkerUhura, 8);
	} else {
		if (!_inCombat) {
			// won: back to the mission
			_randomEncounterType = kRandomEncounterNone;
			_resource->setTxtFileName(_missionName);
			_sound->loadMusicFile("bridge");
		}
		bridgeDefaultAction();
	}
}

// ---- The admiral's review (11dc:2d98) ----

void StarTrekEngine::showMissionReview(int missionId, int textId, int score, int maxScore, bool removeTalker) {
	int pct = 0;
	if (maxScore > 0)
		pct = CLIP(score, 0, maxScore) * 100 / maxScore;

	Common::String description;
	int points;
	MidiTracks track;
	if (pct <= 50) {
		points = 0;
		track = MIDITRACK_13;
		description = "#BRID\\B_199#I'll be frank, Kirk. Starfleet expects more of you than that. Try to do better on your next assignment.";
	} else if (pct <= 70) {
		points = 1;
		track = MIDITRACK_13;
		description = "#BRID\\B_197#A satisfactory performance, Captain, but there's still room for improvement.";
	} else if (pct <= 85) {
		points = 2;
		track = MIDITRACK_11;
		description = "#BRID\\B_214#Well done, Captain. Keep up the good work.";
	} else if (pct <= 99) {
		points = 3;
		track = MIDITRACK_12;
		description = "#BRID\\B_414#The top brass at Starfleet are impressed. Outstanding work, Jim.";
	} else {
		points = 4;
		track = MIDITRACK_14;
		description = "#BRID\\B_195#A perfect mission, Jim! You are a model for all Starfleet!";
	}
	_sound->playMidiMusicTracks(track, kLoopTypeLast);

	Common::String text = Common::String::format(_resource->getLoadedText(textId).c_str(),
		getSpeechSampleForNumber(pct).c_str(), getSpeechSampleForNumber(points).c_str(), pct, points);
	viewscreenTalker("woman");
	viewscreenTalkerText("Admiral", text);
	showTextbox("Admiral", description, 160, 190, 44, 0);
	if (removeTalker)
		removeViewscreenTalker();

	_lastMissionId = missionId;
	_missionPoints[missionId] = points;
	// commendations: more emergency power, stronger weapons and shields
	_extraPowerBonus += points >> 1;
	_extraPowerDuration += points * 15;
	ShipState &s = _enterpriseR3.ship;
	for (int i = 0; i < 4; i++)
		s.systems[kSystemWeapon0 + i].max += points >> 2;
	for (int i = 0; i < 6; i++)
		s.shields[i].max += points >> 1;
}

} // End of namespace StarTrek
