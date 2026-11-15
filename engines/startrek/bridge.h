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

#ifndef STARTREK_BRIDGE_H
#define STARTREK_BRIDGE_H

namespace StarTrek {

enum BridgeMenuEvent {
	kBridgeNone = -1,
	kBridgeKirkCommand = 0,
	kBridgeUnk1 = 1,
	kBridgeUnk2 = 2,
	kBridgeStarfieldFullScreen = 3,
	kBridgeKirkCaptainsLog = 16,
	kBridgeKirkTransporter = 17,
	kBridgeKirkOptions = 18,
	kBridgeSpock = 32,
	kBridgeSpockComputer = 33,
	kBridgeScottyDamageControl = 48,
	kBridgeScottyEmergencyPower = 49,
	kBridgeUhura = 64,
	kBridgeSuluOrbit = 80,
	kBridgeSuluShields = 81,
	kBridgeChekovNavigation = 96,
	kBridgeChekovWeapons = 97,
	kBridgeChekovRepairShields = 112,
	kBridgeChekovRepairPhasers = 113,
	kBridgeChekovRepairPhotonTorpedoes = 114,
	kBridgeChekovRepairSensors = 115,
	kBridgeChekovRepairBridge = 116,
	kBridgeChekovRepairHull = 117,
	kBridgeChekovRepairWarpDrives = 118,
	kBridgeSuluTargetAnalysis = 119
};

enum Planet {
	kPlanetNone = -1,
	kPlanetCenturius = 0,
	kPlanetCameronsStar = 1,
	kPlanetArk7 = 2,      // Chapter 3: Love's Labor Jeopardized (love)
	kPlanetHarlequin = 3, // Chapter 4: Another Fine Mess (mudd)
	kPlanetHarrapa = 4,
	kPlanetElasiPrime = 5,
	kPlanetDigifal = 6, // Chapter 5A: The Feathered Serpent (feather)
	kPlanetStrahkeer = 7,
	kPlanetHrakkour = 8, // Chapter 5B: The Feathered Serpent (trial)
	kPlanetTriRhoNautica = 9,
	kPlanetShivaOmicron = 10,
	kPlanetAlphaProxima = 11, // Chapter 6: The Old Devil Moon (sins)
	kPlanetOmegaMaelstrom = 12,
	kPlanetArgosIV = 13,
	kPlanetBetaMyamid = 14, // Chapter 2: Hijacked (tug)
	kPlanetSirius = 15,
	kPlanetSigmaZhukova = 16,
	kPlanetCastor = 17,
	kPlanetPollux = 18, // Chapter 1: Demon world (demon)
	kPlanetChristgen = 19
};

enum BridgeTalkers {
	kBridgeTalkerNone = -1,
	kBridgeTalkerKirk = 0,
	kBridgeTalkerSpock,
	kBridgeTalkerSulu,
	kBridgeTalkerChekov,
	kBridgeTalkerUhura,
	kBridgeTalkerScotty,
	kBridgeTalkerMcCoy,
	kBridgeTalkerCaptainsLog,
	kBridgeTalkerElasiCaptain,
	kBridgeTalkerElasiCereth,
	kBridgeTalkerAutobeacon,
	kBridgeTalkerHarryMuddsVoice,
	kBridgeTalkerHarryMudd,
	kBridgeTalkerTaraz,
	kBridgeTalkerAdmiralKenka,
	kBridgeTalkerKallarax
};

// The bridge's actions (the original's queue, DS:dc9a): with the space
// simulation's (SpaceEvent) 2, 3, 4 and 7
enum BridgeActionType {
	kBridgeActionTick = 0,
	kBridgeActionMenu = 1,     // an order (BridgeMenuEvent)
	kBridgeActionText = 5,     // a text box with a flag closed
	kBridgeActionBeamedUp = 6  // back from an away mission
};

// The missions' handlers, numbered as the original's (DS:006c)
enum BridgeHandler {
	kBridgeHandlerDemonStart = 0,      // 3c13:0006
	kBridgeHandlerMockBattle = 1,      // 3c13:0181
	kBridgeHandlerDemonDebrief = 2,    // 3c13:03b1
	kBridgeHandlerDemonPollux = 3,     // 3c13:0512
	kBridgeHandlerDemonEnd = 4,        // 3c13:07b5
	kBridgeHandlerTugStart = 5,        // 3cb2:0006
	kBridgeHandlerTugPirate = 6,       // 3cb2:0254
	kBridgeHandlerTugAfterBattle = 7,  // 3cb2:0540
	kBridgeHandlerTugMasada = 8,       // 3cb2:065d
	kBridgeHandlerTugEnd = 9,          // 3cb2:0954
	kBridgeHandlerLoveStart = 10,      // 3ddd:000a
	kBridgeHandlerLoveEnRoute = 11,    // 3ddd:017f
	kBridgeHandlerLoveRomulan = 12,    // 3ddd:033e
	kBridgeHandlerLoveAfterBattle = 13, // 3ddd:057b
	kBridgeHandlerLoveArk7 = 14,       // 3ddd:0692
	kBridgeHandlerLoveEnd = 15,        // 3ddd:086b
	kBridgeHandlerMuddStart = 16,      // 3e8d:0004
	kBridgeHandlerMuddHarlequin = 17,  // 3e8d:02d4
	kBridgeHandlerMuddHarrapa = 18,    // 3e8d:07bb
	kBridgeHandlerFeatherStart = 19,   // 3f59:000a
	kBridgeHandlerFeatherOrbit = 20,   // 3f59:049e
	kBridgeHandlerFeatherKlrta = 21,   // 3f59:0931
	kBridgeHandlerTrialStart = 22,     // 3fed:000c
	kBridgeHandlerTrialEnd0 = 23,      // 3fed:01e5
	kBridgeHandlerTrialEnd1 = 24,      // 3fed:02f1
	kBridgeHandlerTrialEnd3 = 25,      // 3fed:041a
	kBridgeHandlerTrialEnd4 = 26,      // 3fed:0521
	kBridgeHandlerTrialKlrta = 27,     // 3fed:06ba
	kBridgeHandlerVengStart = 28,      // 40e6:0006
	kBridgeHandlerVengBattle = 29,     // 40e6:05d4
	kBridgeHandlerSinsStart = 30,      // 4074:000e
	kBridgeHandlerSinsEnd = 31         // 4074:0307
};

enum RandomEncounterType {
	kRandomEncounterNone = 0,
	kRandomEncounterKlingon = 1,
	kRandomEncounterRomulan = 2,
	kRandomEncounterElasi = 3
};

} // End of namespace StarTrek

#endif
