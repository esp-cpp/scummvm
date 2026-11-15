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

#ifndef STARTREK_SPACE_H
#define STARTREK_SPACE_H

#include "fixedint.h"

namespace StarTrek {

template<typename T>
struct TPoint {
	T x;
	T y;
	T z;

	TPoint() : x(0), y(0), z(0) {}
	TPoint(T _x, T _y, T _z) : x(_x), y(_y), z(_z) {}

	int32 getDiagonal() {
		return (int32)sqrt((double)x * x + y * y + z * z);
	}

	TPoint<T> operator+(const TPoint<T> &p) const {
		TPoint<T> p2;
		p2.x = x + p.x;
		p2.y = y + p.y;
		p2.z = z + p.z;
		return p2;
	}
	TPoint<T> operator-(const TPoint<T> &p) const {
		TPoint<T> p2;
		p2.x = x - p.x;
		p2.y = y - p.y;
		p2.z = z - p.z;
		return p2;
	}
	void operator+=(const TPoint &p) {
		x += p.x;
		y += p.y;
		z += p.z;
	}
	void operator-=(const TPoint &p) {
		x -= p.x;
		y -= p.y;
		z -= p.z;
	}
	T &operator[](int i) {
		if (i == 0)
			return x;
		else if (i == 1)
			return y;
		else if (i == 2)
			return z;

		// Unknown Out of Range
		assert(false);
		return x;
	}
	T operator[](int i) const {
		if (i == 0)
			return x;
		else if (i == 1)
			return y;
		else if (i == 2)
			return z;

		// Unknown Out of Range
		assert(false);
		return x;
	}
};

typedef TPoint<int32> Point3;
typedef TPoint<int16> Point3_Short;
typedef TPoint<Fixed14> Point_Fixed14;


template<typename T>
struct TMatrix {
private:
	T m[3];

public:
	TMatrix() {
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				m[i][j] = 0;
	}
	T &operator[](int i) {
		return m[i];
	};
	T operator[](int i) const {
		return m[i];
	};

	TMatrix operator*(const TMatrix &m2) const {
		TMatrix ret;
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				ret[i][j] = 0;
				for (int a = 0; a < 3; a++)
					ret[i][j] += m[i][a] * m2[a][j];
			}
		}
		return ret;
	}

	void operator*=(const TMatrix &m2) {
		*this = *this * m2;
	}

	TMatrix<T> invert() const {
		TMatrix<T> ret;
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				ret[i][j] = m[j][i];
			}
		}
		return ret;
	}
};

typedef TMatrix<Point_Fixed14> Matrix;

struct Star {
	bool active;
	Point3 pos;
};

// Struct for objects in space.
// TODO: what does this stand for? Maybe rename it.
// A ship's shields (one per side) and systems: (enabled, maximum, current)
struct ShipSystem {
	int16 enabled = 0;
	int16 max = 0;
	int16 cur = 0;
};

// The ship's systems, as the damage tables (DS:1192) name them
enum ShipSystemIndex {
	kSystemHull = 0,
	kSystemHelm = 1,     // ("bridge") turning and acceleration scale with it
	kSystemWeapon0 = 2,  // the weapons' mounts, one per weapon (photon
	                     // torpedoes 2-3, phasers 4-5 on the Enterprise)
	kSystemSensors = 6,
	kSystemPower0 = 7,   // power: 7-9
	kNumShipSystems = 10
};

// One of a ship's weapons, from its .DSC (0x18 bytes)
struct ShipWeapon {
	uint32 nextFire = 0;   // 0x0: the tick it may fire again
	int16 life = 0;        // 0x4: of its shots, in ticks
	int16 speed = 0;       // 0x6: of its shots (plus the ship's)
	int16 interval = 0;    // 0x8: ticks between shots
	int32 offset[3] = {};  // 0xa: where on the ship it fires from
	int16 kind = 0;        // 0x16: the shot's field20: 0 photon torpedo, 1 phaser, 2 plasma
};

// A ship's state in space (the original's 0x1ce byte ships, DS:b708 and the
// Enterprise at DS:d49e; the offsets are those)
struct ShipState {
	ShipSystem shields[6];             // 0xa0: by side (findShipSide)
	ShipSystem systems[kNumShipSystems]; // 0xc4
	int32 maxTurnRate = 0;             // 0x100 (16.16; the integer part indexes the turn tables)
	int32 turnAccel = 0;               // 0x104
	int32 maxSpeed = 0;                // 0x108
	int32 speedAccel = 0;              // 0x10c
	int32 requestedSpeed = 0;          // 0x110
	int32 targetTurnRate[3] = {};      // 0x114: round x (pitch), y (yaw), z (roll)
	int32 speedTarget = 0;             // 0x120: what the power allows of the request
	int32 speed = 0;                   // 0x124
	int32 turnRate[3] = {};            // 0x128
	int32 turned[3] = {};              // 0x134: how far it's turned (1/1024 turns, 16.16)
	int16 timer = 0;                   // 0x140: counts down each tick (maneuvers)
	int16 power = 0;                   // 0x142: left over this tick
	int16 shieldsRequested = 0;        // 0x144
	int16 shieldsUp = 0;               // 0x146
	int16 weaponsRequested = 0;        // 0x148
	int16 weaponsArmed = 0;            // 0x14a
	int32 distance = 0;                // 0x14c: to the Enterprise (enemies)
	int32 closing = 0;                 // 0x150: change of the distance this tick
	int16 maneuvering = 0;             // 0x154
	int16 maneuver = 0;                // 0x156: which (2f0d:2781, 28bf, 2b51)
	int16 maneuverStep = 0;            // 0x15a
	ShipWeapon weapons[4];             // 0x15c
	int32 closeRange = 0;              // 0x1bc: nearer than this, it maneuvers
	int16 canCloak = 0;                // 0x1c0
	int16 cloakRequested = 0;          // 0x1c2
	int16 cloaked = 0;                 // 0x1c4
	int16 cloakFading = 0;             // 0x1c6
	int16 destroyed = 0;               // 0x1c8: exploding
	int16 explosionTimer = 0;          // 0x1ca
	int16 leaving = 0;                 // 0x1cc: flies away and is gone
};

struct R3 {
	Point3 pos; // 0x0
	Matrix matrix; // 0xc
	int16 field1e = 0; // 0x1e (shape type: 1, 2 = ship, 3 = flat bitmap, 4 = explosion)
	int16 field20 = 0; // 0x20 (for ships: the class, an index into the ship names)
	int16 field22 = 0; // 0x22 (radius, for culling)
	int16 field24 = 0; // 0x24 (size)
	Point3_Short speed; // 0x26
	int32 funcPtr1 = 0; // 0x2c
	int32 funcPtr2 = 0; // 0x30
	int16 field34 = 0; // 0x34
	Point3 field36; // 0x36
	Matrix matrix2; // 0x42
	int32 field54 = 0; // 0x54 (used for sorting by draw priority?)
	int16 field58 = 0; // 0x58
	int16 field5a = 0; // 0x5a
	Bitmap *bitmap = nullptr; // 0x68 (was: shpFile)
	// 0x6a (was: bitmapOffset)
	// For ships (field1e 1 / 2): the view of the ship the bitmap is
	// (selectShipView(), the original's funcPtr1 for them), its orientation
	// as rendered, and whether it's drawn mirrored.
	bool shipViews = false;
	// For planets (field1e 1, the original's funcPtr1 2d4b:0e10): the
	// planet's size, whether it's a small one (32x32 texture), and the seed
	// and colour ramp its texture is made from (a fractal); the texture
	// itself once made
	bool planet = false;
	int16 planetSize = 0; // 0xa0
	int16 planetSmall = 0; // 0xa2
	uint16 planetSeed = 0; // 0xb0
	uint16 planetColor = 0; // 0xb2
	byte *planetTexture = nullptr;
	int16 viewMatrix[3][3] = {}; // 0x6c
	bool viewMirrored = false; // 0x7e
	int16 view = -1; // 0x66
	double field80 = 0; // 0x80
	double field88 = 0; // 0x88
	double field90 = 0; // 0x90
	double field98 = 0; // 0x98

	// A ship (field1e 2)
	ShipState ship;
	// A shot (field1e 3, the original's 0xa8 byte objects at DS:7420): its
	// ship, ticks left, damage, colour
	R3 *owner = nullptr; // 0xa0
	int16 life = 0;      // 0xa2
	int16 damage = 0;    // 0xa4
	int16 color = 0;     // 0xa6
};

// Maximum number of stars visible at once in the starfields
#define NUM_STARS 16

// Maximum number of R3 objects in space at once
#define NUM_SPACE_OBJECTS 0x30
// Enemy ships at once (DS:b708), and shots (DS:7420)
#define NUM_ENEMY_SHIPS 4
#define NUM_SHOTS 0x20

// The space simulation's messages for the bridge (the original's events)
enum SpaceEvent {
	kSpaceEventShieldsDown = 2,
	kSpaceEventWeaponsDown = 3,
	kSpaceEventRepaired = 7
};

// The original's 3D arithmetic, exactly (space.cpp): angles in 1/1024ths
// of a turn, matrices and sines 2.14 fixed point
int16 trekSin(int angle);
int16 trekCos(int angle);
int16 trekAtan2(int32 y, int32 x);
int16 trekAsin(int16 v);
int16 trekAcos(int16 v);
/// a * b, rows times columns, the sum shifted once (2407:0b04's partner 3a22:0181)
Matrix trekMatrixMultiply(const Matrix &a, const Matrix &b);
/// m turned round its y axis: Ry * m (2407:09f4)
void trekPreRotateY(Matrix &m, int16 angle);
/// m turned round its x axis: Rx * m (2407:096c)
void trekPreRotateX(Matrix &m, int16 angle);
/// m * Ry (2407:085c)
void trekPostRotateY(Matrix &m, int16 angle);
/// m * Rx (2407:07d4)
void trekPostRotateX(Matrix &m, int16 angle);
/// A matrix looking along dir with up as near up as it can be (2407:0d67).
/// False when dir is 0 or along up.
bool trekLookAt(const Point3 &dir, const Point_Fixed14 &up, Matrix &out);
/// The length of a vector (2407:0c05)
int32 trekLength(const Point3 &p);

} // End of namespace StarTrek

#endif
