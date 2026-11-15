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

// Space as the bridge's viewscreen and the full screen show it, and the 3D
// sequences: the mission start fly-bys, entering and leaving orbit. As the
// original (DOS CD) does them; its functions are named by address.

#include "startrek/bridge.h"
#include "startrek/resource.h"
#include "startrek/sound.h"
#include "startrek/startrek.h"

namespace StarTrek {

// The star systems' planets (3383:0008): their textures' seeds and colours
static const struct {
	uint16 seed;
	uint16 color;
} kStarSystemPlanets[] = {
	{ 453, 6 },   // Centurius
	{ 293, 10 },  // Camerons Star
	{ 517, 11 },  // Ark 7
	{ 318, 12 },  // Harlequin
	{ 924, 18 },  // Harrapa
	{ 341, 7 },   // Elasi Prime
	{ 856, 8 },   // Digifal
	{ 135, 0 },   // Strahkeer
	{ 392, 14 },  // Hrakkour
	{ 643, 13 },  // Tri-Rho Nautica
	{ 832, 12 },  // Shiva Omicron
	{ 341, 11 },  // Alpha Proxima
	{ 839, 10 },  // Omega Maelstrom
	{ 932, 7 },   // Argos IV
	{ 645, 14 },  // Beta Myamid
	{ 234, 15 },  // Sirius
	{ 341, 16 },  // Sigma Zhukova
	{ 883, 17 },  // Castor
	{ 752, 9 },   // Pollux
	{ 234, 9 },   // Christgen
};

// The game's start (2d4b:0c65, then added to the shapes): a planet ahead
void StarTrekEngine::initSpaceObjects() {
	freePlanetR3(&_planetR3);
	_planetR3 = R3();
	_planetR3.matrix = initMatrix();
	_planetR3.pos = Point3(0, 0, 15300);
	initPlanetR3(&_planetR3, 500, 0, 714, 10);
	addR3(&_planetR3);
	_cameraOnEnterprise = true;
}

void StarTrekEngine::setStarSystemPlanet(int planet) {
	if (planet < 0 || planet >= ARRAYSIZE(kStarSystemPlanets)) {
		setRandomPlanet();
		return;
	}
	initPlanetR3(&_planetR3, 500, 0, kStarSystemPlanets[planet].seed, kStarSystemPlanets[planet].color);
}

// (2d4b:0d14)
void StarTrekEngine::setRandomPlanet() {
	uint32 seed = _frameIndex;
	auto rnd = [&seed]() {
		seed = seed * 214013 + 2531011;
		return (int16)((seed >> 16) & 0x7fff);
	};
	int16 texture = rnd() % 1000;
	int16 color = rnd() % 16 + 6;
	initPlanetR3(&_planetR3, 500, 0, texture, color);
}

// The 3D view across the whole screen (11dc:26ed, 11dc:086d)
void StarTrekEngine::showFullScreenSpace() {
	if (_spaceFullScreen)
		return;
	removeDrawnActorsFromScreen();
	_gfx->delSprite(&_starfieldSprite);
	_gfx->toggleMouse(false);
	_gfx->clearScreenAndPriBuffer();
	initStarfield(2, 2, 0x13f, 199, 0x200 / 4);
	initStarfieldSprite(&_starfieldSprite, new StubBitmap(0, 0), _starfieldRect);
	_spaceFullScreen = true;
}

// Back to the bridge, its viewscreen showing space (11dc:2717, 11dc:08e6)
void StarTrekEngine::showBridgeViewscreen() {
	if (!_spaceFullScreen)
		return;
	_gfx->delSprite(&_starfieldSprite);
	_gfx->clearScreenAndPriBuffer();
	loadBridge();
	_gfx->toggleMouse(true);
	_spaceFullScreen = false;
}

void StarTrekEngine::renderSpace(bool staticNoise) {
	if (_cameraOnEnterprise) {
		// (11dc:1f75) the viewscreen is the Enterprise's
		_starfieldPosition = _enterpriseR3.pos;
		_someMatrix = _enterpriseR3.matrix;
	}
	updateStarfieldAndShips(staticNoise);
	_gfx->drawAllSprites();
}

void StarTrekEngine::updateBridgeViewscreen() {
	spaceCombatTick();
	// The Enterprise exploding: watched from outside, across the screen
	// (11dc:042c)
	if (_enterpriseR3.ship.destroyed && _enterpriseR3.ship.explosionTimer >= 0 && _cameraOnEnterprise) {
		showFullScreenSpace();
		Matrix cam = _enterpriseR3.matrix;
		trekPreRotateY(cam, 0x40);
		static const int32 kOffset[3] = { -150, 50, -500 };
		Point3 offset;
		for (int j = 0; j < 3; j++) {
			int32 sum = 0;
			for (int i = 0; i < 3; i++)
				sum += kOffset[i] * cam[i][j].raw();
			offset[j] = (int16)(sum >> 14);
		}
		_starfieldPosition = _enterpriseR3.pos + offset;
		_someMatrix = cam;
		_cameraOnEnterprise = false;
	}
	// (the planet turns, 11dc:1c82)
	trekPreRotateY(_planetR3.matrix, 4);
	renderSpace(viewscreenStatic());
}

void StarTrekEngine::waitSpaceTick() {
	_gfx->drawAllSprites();
	waitForNextTick(false);
	_frameIndex++;
}

// Entering standard orbit (346c:048f): the Enterprise slows past the
// camera, which turns to follow it; then the view from orbit
void StarTrekEngine::enterOrbitSequence() {
	_enterpriseR3.pos = _planetR3.pos + Point3(0x226, 0, -0x802);
	_enterpriseR3.matrix = initMatrix();
	Point3 camera = _planetR3.pos + Point3(900, 0, 0);
	Matrix cameraMatrix = initMatrix();
	_cameraOnEnterprise = false;
	_starfieldPosition = camera;
	_gfx->clearScreenAndPriBuffer();

	int32 speed = 100;
	for (int frame = 0; frame != 0x25; frame++) {
		_enterpriseR3.pos.z += speed;
		speed -= 2;

		// (2f0d:245f) where the Enterprise is, from the camera
		Point3 d = matrixMult(_enterpriseR3.pos - camera, cameraMatrix.invert());
		int16 heading = trekAtan2(d.x, d.z);
		trekPreRotateY(cameraMatrix, heading);
		_someMatrix = cameraMatrix;

		if (frame < 0x20)
			_gfx->incPaletteFadeLevel();
		else
			_gfx->decPaletteFadeLevel();
		renderSpace();
		waitSpaceTick();
	}
	_gfx->fadeoutScreen();
	_gfx->clearScreenAndPriBuffer();

	// in orbit, just above the planet, looking down at it
	camera = _planetR3.pos;
	camera.z -= _planetR3.field22 + 0xfa;
	cameraMatrix = initMatrix();
	trekPreRotateY(cameraMatrix, 0x40);
	trekPreRotateX(cameraMatrix, -0x20);
	_enterpriseR3.pos = camera;
	_enterpriseR3.matrix = cameraMatrix;
	stopShip(&_enterpriseR3);
	_cameraOnEnterprise = true;
}

// Leaving orbit (346c:06c6): the Enterprise turns away from the planet and
// speeds off, the camera watching it go
void StarTrekEngine::leaveOrbitSequence() {
	_enterpriseR3.pos = _planetR3.pos + Point3(-300, 0, -0x226);
	_enterpriseR3.matrix = initMatrix();
	trekPreRotateY(_enterpriseR3.matrix, 0x100);
	_someMatrix = initMatrix();
	_starfieldPosition = _enterpriseR3.pos;
	_starfieldPosition.z -= 0x1fe;
	_cameraOnEnterprise = false;
	_gfx->clearScreenAndPriBuffer();

	int32 speed = 0x19;
	for (int frame = 0; frame != 0x40; frame++) {
		if (frame < 0x20)
			trekPreRotateY(_enterpriseR3.matrix, 8);
		for (int i = 0; i < 3; i++)
			_enterpriseR3.pos[i] += ((int32)_enterpriseR3.matrix[2][i].raw() * speed) >> 14;

		Matrix look;
		if (trekLookAt(_enterpriseR3.pos - _starfieldPosition, _enterpriseR3.matrix[1], look))
			_someMatrix = look;

		if (frame < 0x3c)
			_gfx->incPaletteFadeLevel();
		else
			_gfx->decPaletteFadeLevel();
		renderSpace();
		waitSpaceTick();
		speed += 4;
	}
	_gfx->fadeoutScreen();
	_gfx->clearScreenAndPriBuffer();

	// (stopped, nudged on: 346c:091c, 2f0d:0d22)
	stopShip(&_enterpriseR3);
	_enterpriseR3.ship.speed = MIN<int32>(0x190000, _enterpriseR3.ship.speedAccel);
	_cameraOnEnterprise = true;
}

// Sulu, orbit (11dc:2741)
void StarTrekEngine::orbitPlanet() {
	const char *enteringOrbitText = "#BRID\\B_333#Entering standard orbit.";
	const char *leavingOrbitText = "#BRID\\B_335#Leaving orbit.";
	bool entering = !_enterpriseState.inOrbit;

	_gfx->fadeoutScreen();
	showFullScreenSpace();
	if (entering)
		enterOrbitSequence();
	else
		leaveOrbitSequence();
	showBridgeViewscreen();
	renderSpace();

	_enterpriseState.inOrbit = entering;
	showTextboxBridge(kBridgeTalkerSulu, entering ? enteringOrbitText : leavingOrbitText);
}

// The Enterprise flying past at a mission's start, its title, Kirk's
// narration (11dc:39a7, then the ticks of 11dc:3b21)
void StarTrekEngine::showMissionStartEnterpriseFlyby(Common::String sequence, Common::String name) {
	setRandomPlanet();
	_sound->playMidiMusicTracks(MIDITRACK_11);
	_sound->playVoc("bridloop");
	showFullScreenSpace();

	// (the camera and the Enterprise move in straight lines, the camera
	// keeping it in view: 11dc:27c8)
	int16 velocity[2][3] = { { 100, 0, 0 }, { 0x7d, 0, 0 } };
	int16 start[2][3] = { { 0x7b6, 0, 0x2d0 }, { 0, 0, 0 } };
	_someMatrix = initMatrix();
	_enterpriseR3.matrix = initMatrix();
	trekPreRotateY(_enterpriseR3.matrix, 0x100);
	stopShip(&_enterpriseR3);
	bool lookAtEnterprise = true;
	_cameraOnEnterprise = false;

	Sprite title;
	Bitmap *titleBitmap = new Bitmap(_resource->loadBitmapFile(name));
	titleBitmap->xoffset = titleBitmap->width / 2;
	titleBitmap->yoffset = titleBitmap->height / 2;
	bool titleShown = false;

	for (int t = 2; t <= 0x96; t++) {
		for (int i = 0; i < 3; i++)
			_enterpriseR3.pos[i] = (int16)(velocity[1][i] * t + start[1][i]);
		if (lookAtEnterprise) {
			for (int i = 0; i < 3; i++)
				_starfieldPosition[i] = (int16)(velocity[0][i] * t + start[0][i]);
			Matrix look;
			if (trekLookAt(_enterpriseR3.pos - _starfieldPosition, _enterpriseR3.matrix[1], look))
				_someMatrix = look;
		}

		switch (t) {
		case 0x41:
			if (!sequence.empty() && _sfxEnabled && _sfxWorking)
				_sound->playSpeech(sequence);
			_gfx->addSprite(&title);
			title.setXYAndPriority(0xa0, 0x3c, 0xe);
			title.setBitmap(titleBitmap);
			titleShown = true;
			break;
		case 0x60:
			if (titleShown) {
				title.dontDrawNextFrame();
				_gfx->drawAllSprites();
				_gfx->delSprite(&title);
				titleShown = false;
			}
			break;
		case 0x69:
			// to the bridge, its viewscreen the Enterprise's, cruising
			showBridgeViewscreen();
			lookAtEnterprise = false;
			_cameraOnEnterprise = true;
			// (the Enterprise at a quarter of her speed from now)
			start[1][0] = velocity[1][0] * t;
			velocity[1][0] = (int16)(_enterpriseR3.ship.maxSpeed >> 18);
			_enterpriseR3.ship.requestedSpeed = _enterpriseR3.ship.maxSpeed >> 2;
			break;
		default:
			break;
		}

		if (_spaceFullScreen)
			renderSpace();
		else
			updateBridgeViewscreen();
		// (fading in, a tick at a time: 11dc:1c82)
		if (t > 2)
			_gfx->incPaletteFadeLevel();
		waitSpaceTick();
	}
	if (titleShown)
		_gfx->delSprite(&title);
	_cameraOnEnterprise = true;
}

// Where the stars are on the star map (DS:1220), a star's index its kPlanet...
static const int16 kStarMapStars[][2] = {
	{ 14, 16 }, { 28, 59 }, { 62, 32 }, { 102, 38 }, { 116, 33 },
	{ 167, 18 }, { 245, 27 }, { 263, 8 }, { 294, 37 }, { 260, 63 },
	{ 253, 118 }, { 230, 122 }, { 237, 93 }, { 220, 77 }, { 177, 62 },
	{ 167, 111 }, { 118, 79 }, { 90, 102 }, { 73, 111 }, { 111, 124 },
	{ 21, 95 }, { 49, 28 }, { 81, 153 }, { 40, 133 }, { 25, 121 },
	{ 112, 159 }, { 154, 169 }, { 160, 152 }, { 184, 135 }, { 213, 166 },
	{ 271, 165 }, { 269, 136 }, { 297, 145 }, { 303, 103 }, { 279, 111 },
	{ 280, 85 }, { 300, 14 }, { 217, 33 }, { 218, 55 }, { 166, 85 },
	{ 50, 44 }, { 151, 44 }, { 156, 16 }, { 83, 72 }, { 150, 102 }
};

// The star nearest a point on the map (3383:04fb)
static int nearestStar(Common::Point p) {
	int nearest = 0;
	int best = 0x7fff;
	for (int i = 0; i < ARRAYSIZE(kStarMapStars); i++) {
		int distance = ABS(kStarMapStars[i][0] - p.x) + ABS(kStarMapStars[i][1] - p.y);
		if (distance < best) {
			nearest = i;
			best = distance;
		}
	}
	return nearest;
}

// The star map (11dc:269c, 3383:027e): a circle on the star nearest the
// cursor; a click, or Enter, Space, F1 or F2, picks it
int StarTrekEngine::chooseStarMapDestination() {
	_gfx->fadeoutScreen();
	showFullScreenSpace();
	_gfx->delSprite(&_starfieldSprite);
	_gfx->setBackgroundImage("starmap");
	_gfx->copyBackgroundScreen();
	_gfx->setMouseBitmap("cross");
	_gfx->toggleMouse(true);

	int star = nearestStar(_gfx->getMousePos());
	Sprite circle;
	_gfx->addSprite(&circle);
	circle.setBitmap(new Bitmap(_resource->loadBitmapFile("circle")));
	circle.setXYAndPriority(kStarMapStars[star][0], kStarMapStars[star][1], 0xe);
	_gfx->drawAllSprites();
	_gfx->fadeinScreen();

	bool chosen = false;
	while (!chosen && !shouldQuit()) {
		TrekEvent event;
		if (!popNextEvent(&event))
			continue;

		switch (event.type) {
		case TREKEVENT_TICK:
			_frameIndex++;
			break;
		case TREKEVENT_MOUSEMOVE:
		case TREKEVENT_LBUTTONDOWN:
			// (a touch moves the circle as it picks)
			star = nearestStar(_gfx->getMousePos());
			circle.setXYAndPriority(kStarMapStars[star][0], kStarMapStars[star][1], 0xe);
			_gfx->drawAllSprites();
			chosen = event.type == TREKEVENT_LBUTTONDOWN;
			break;
		case TREKEVENT_RBUTTONDOWN:
			chosen = true;
			break;
		case TREKEVENT_KEYDOWN:
			switch (event.kbd.keycode) {
			case Common::KEYCODE_RETURN:
			case Common::KEYCODE_KP_ENTER:
			case Common::KEYCODE_SPACE:
			case Common::KEYCODE_F1:
			case Common::KEYCODE_F2:
				chosen = true;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
	}

	circle.dontDrawNextFrame();
	_gfx->drawAllSprites();
	_gfx->delSprite(&circle);
	_gfx->popMouseBitmap();
	_gfx->toggleMouse(false);
	initStarfieldSprite(&_starfieldSprite, new StubBitmap(0, 0), _starfieldRect);

	setStarSystemPlanet(star);
	warpSequence();
	_gfx->loadPalette("bridge");
	return star;
}

// Going to warp (346c:0006): the camera, ahead of the Enterprise and to its
// side, turns to watch it fly past; it turns, flashes into warp and is
// gone. The Enterprise is then where the camera was, facing its way.
void StarTrekEngine::warpSequence() {
	_gfx->fadeoutScreen();
	_gfx->clearScreenAndPriBuffer();
	_cameraOnEnterprise = false;

	_enterpriseR3.pos = Point3(0, 0, 0);
	_enterpriseR3.matrix = initMatrix();
	trekPreRotateY(_enterpriseR3.matrix, 0x100);
	Point3 camera;
	for (int i = 0; i < 3; i++) {
		camera[i] = _enterpriseR3.pos[i] + (((int32)_enterpriseR3.matrix[2][i].raw() * 2000) >> 14)
			+ (((int32)_enterpriseR3.matrix[0][i].raw() * 500) >> 14);
	}
	Matrix cameraMatrix = _enterpriseR3.matrix;
	_starfieldPosition = camera;

	_sound->playMidiMusicTracks(MIDITRACK_12);

	R3 flash;
	flash.matrix = initMatrix();
	flash.field1e = 3;
	flash.field22 = 0xe1;
	flash.field24 = 0x15;

	for (int t = 0; t != 0x40 && !shouldQuit(); t++) {
		// (2f0d:245f) the camera turns to the Enterprise
		Point3 d = matrixMult(_enterpriseR3.pos - camera, cameraMatrix.invert());
		trekPreRotateY(cameraMatrix, trekAtan2(d.x, d.z));
		_someMatrix = cameraMatrix;

		if (t < 0x30)
			trekPreRotateY(_enterpriseR3.matrix, -1);
		for (int i = 0; i < 3; i++)
			_enterpriseR3.pos[i] += ((int32)_enterpriseR3.matrix[2][i].raw() * 100) >> 14;

		if (t == 0x34)
			delR3(&_enterpriseR3);
		if (t == 0x2c)
			_sound->playSoundEffectIndex(kSfxWarp);
		if (t >= 0x30) {
			// the flash, where the Enterprise is
			flash.pos = _enterpriseR3.pos;
			delete flash.bitmap;
			flash.bitmap = new Bitmap(_resource->loadBitmapFile(Common::String::format("warp%02d", t - 0x30)));
			if (t == 0x30)
				addR3(&flash);
		}

		_gfx->incPaletteFadeLevel();
		renderSpace();
		waitSpaceTick();
	}

	_gfx->fadeoutScreen();
	_gfx->clearScreenAndPriBuffer();
	delR3(&flash);

	_enterpriseR3.pos = camera;
	_enterpriseR3.matrix = cameraMatrix;
	addR3(&_enterpriseR3);
	_enterpriseR3.field1e = 2;
	_cameraOnEnterprise = true;
	_sound->playVoc("bridloop");
}

} // End of namespace StarTrek
