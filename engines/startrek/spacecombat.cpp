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

// Ships in space: their power, flight, repairs, weapons and damage, and the
// enemies' minds. As the original (DOS CD) does them, segment 2f0d; its
// functions are named by address.

#include "common/debug.h"
#include "common/serializer.h"

#include "startrek/bridge.h"
#include "startrek/resource.h"
#include "startrek/sound.h"
#include "startrek/startrek.h"

namespace StarTrek {

// The original's tables (2407:0004), made as it makes them: the doubles
// summed, the products truncated
struct CombatTables {
	int16 sine[0x101];     // DS:0000, a quarter turn in 1/1024ths
	int16 tangent[0x81];   // DS:0a02, an eighth of a turn
	int16 turnSin[0x21];   // DS:593a: turning i/1024 radians a tick
	int16 turnCos[0x21];   // DS:5590
	int32 turnAngle[0x21]; // DS:8ade: that in 1/1024 turns, 16.16

	CombatTables() {
		const double step = 0.00613592315234375;
		double a = 0.0;
		for (int i = 0; i < 0x101; i++) {
			sine[i] = (int16)(sin(a) * 16384.0);
			a += step;
		}
		a = 0.0;
		for (int i = 0; i < 0x81; i++) {
			tangent[i] = (int16)(tan(a) * 16384.0);
			a += step;
		}
		for (int i = 0; i < 0x21; i++) {
			double r = (16.0 * i) / 16384.0;
			turnSin[i] = (int16)(sin(r) * 16384.0);
			turnCos[i] = (int16)(cos(r) * 16384.0);
			turnAngle[i] = (int32)(r * 512.0 / 3.141592654 * 65536.0);
		}
	}
};

static const CombatTables &combatTables() {
	static const CombatTables tables;
	return tables;
}

// (a * b) >> 14, 2.14 fixed point (3a22:023c)
static int16 fx14Mul(int16 a, int16 b) {
	return (int16)(((int32)a * b) >> 14);
}

// (a << 14) / b (3a22:0227)
static int16 fx14Div(int16 a, int16 b) {
	if (b == 0)
		return 0;
	return (int16)(((int32)a << 14) / b);
}

static int32 lshr(int32 v, int n) {
	return v >> n;
}

// asin, 2.14 -> 1/1024 turns (2407:06e4): a search of the sine table
static int16 tableAsin(int16 v) {
	const int16 *t = combatTables().sine;
	int sign = 1;
	if (v < 0) {
		sign = -1;
		v = -v;
	}
	int lo = 0, hi = 0x100, mid = 0;
	while (lo <= hi) {
		mid = (lo + hi) >> 1;
		if (v < t[mid])
			hi = mid - 1;
		else if (v > t[mid])
			lo = mid + 1;
		else
			break;
	}
	return sign < 0 ? -mid : mid;
}

// The angle of (x, z): 0 along z, 0x100 along x (2407:0468). Like the
// original it divides the low 16 bits.
static int16 tableAtan2(int32 x, int32 z) {
	if (x == 0 && z == 0)
		return 0;
	bool zNeg = z <= 0, xNeg = x <= 0;
	int32 ax = ABS(x), az = ABS(z);
	bool zSmaller = az <= ax;
	int16 e = zSmaller ? fx14Div((int16)az, (int16)ax) : fx14Div((int16)ax, (int16)az);
	const int16 *t = combatTables().tangent;
	int lo = 0, hi = 0x80, mid = 0;
	while (lo <= hi) {
		mid = (lo + hi) >> 1;
		if (e < t[mid])
			hi = mid - 1;
		else if (e > t[mid])
			lo = mid + 1;
		else
			break;
	}
	if (zSmaller) {
		if (!zNeg && !xNeg)
			return 0x100 - mid;
		if (zNeg && !xNeg)
			return mid + 0x100;
		if (zNeg && xNeg)
			return -0x100 - mid;
		return mid - 0x100;
	}
	if (!zNeg && !xNeg)
		return mid;
	if (!zNeg && xNeg)
		return -mid;
	if (zNeg && !xNeg)
		return 0x200 - mid;
	return mid - 0x200;
}

// A quick distance: the longest + a third of the middle + a quarter of the
// shortest, of the low 16 bits (3a22:013e)
static int32 approxDistance(const Point3 &v) {
	int16 a = ABS((int16)v.x), b = ABS((int16)v.y), c = ABS((int16)v.z);
	if (a < b)
		SWAP(a, b);
	if (a < c)
		SWAP(a, c);
	if (b < c)
		SWAP(b, c);
	return (int16)((((uint32)(uint16)b * 0x5555) >> 16) + a + ((uint16)c >> 2));
}

// The length of the low 16 bits (2407:0c05)
static int16 vectorLength(const Point3 &v) {
	int32 x = (int16)v.x, y = (int16)v.y, z = (int16)v.z;
	return (int16)sqrt((double)(uint32)(x * x + y * y + z * z));
}

// v in the ship's own axes: x right, y up, z ahead (2407:0b04 + 3a22:00de)
static Point3 toShipAxes(const Point3 &v, const Matrix &m) {
	Point3 out;
	for (int j = 0; j < 3; j++) {
		int32 sum = 0;
		for (int i = 0; i < 3; i++)
			sum += (int32)(int16)v[i] * m[j][i].raw();
		out[j] = (int16)(sum >> 14);
	}
	return out;
}

// v given in the ship's axes, in the world's (3a22:00de with the matrix)
static Point3 fromShipAxes(const int32 v[3], const Matrix &m) {
	Point3 out;
	for (int j = 0; j < 3; j++) {
		int32 sum = 0;
		for (int i = 0; i < 3; i++)
			sum += (int32)(int16)v[i] * m[i][j].raw();
		out[j] = (int16)(sum >> 14);
	}
	return out;
}

int StarTrekEngine::combatRand() {
	return _randomSource.getRandomNumber(0x7fff);
}

// Turn by the table's i/1024 radians round x, y or z: R * m (2407:0f9e,
// 0ff1, 1044)
static void turnMatrix(Matrix &m, int axis, int16 s, int16 c) {
	Matrix r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r[i][j] = Fixed14::fromRaw(i == j ? 0x4000 : 0);
	switch (axis) {
	case 0:
		r[1][1] = Fixed14::fromRaw(c);
		r[1][2] = Fixed14::fromRaw(s);
		r[2][1] = Fixed14::fromRaw(-s);
		r[2][2] = Fixed14::fromRaw(c);
		break;
	case 1:
		r[0][0] = Fixed14::fromRaw(c);
		r[0][2] = Fixed14::fromRaw(-s);
		r[2][0] = Fixed14::fromRaw(s);
		r[2][2] = Fixed14::fromRaw(c);
		break;
	default:
		r[0][0] = Fixed14::fromRaw(c);
		r[0][1] = Fixed14::fromRaw(s);
		r[1][0] = Fixed14::fromRaw(-s);
		r[1][1] = Fixed14::fromRaw(c);
		break;
	}
	m = trekMatrixMultiply(r, m);
}

// A ship's class, from its .DSC (2f0d:0e49)
bool StarTrekEngine::loadShipClass(R3 *r3, const Common::String &name) {
	Common::MemoryReadStreamEndian *file = _resource->loadFile(name + ".DSC", 0, false);
	if (!file)
		return false;
	r3->field20 = file->readSint16();
	r3->field22 = file->readSint16();
	r3->field24 = file->readSint16();
	ShipState &s = r3->ship;
	for (int i = 0; i < 6; i++) {
		s.shields[i].enabled = file->readSint16();
		s.shields[i].max = file->readSint16();
		s.shields[i].cur = file->readSint16();
	}
	for (int i = 0; i < kNumShipSystems; i++) {
		s.systems[i].enabled = file->readSint16();
		s.systems[i].max = file->readSint16();
		s.systems[i].cur = file->readSint16();
	}
	// 0x66: the flight model (0x21 words, to the ship's 0x100)
	s.maxTurnRate = file->readSint32();
	s.turnAccel = file->readSint32();
	s.maxSpeed = file->readSint32();
	s.speedAccel = file->readSint32();
	s.requestedSpeed = file->readSint32();
	for (int i = 0; i < 3; i++)
		s.targetTurnRate[i] = file->readSint32();
	s.speedTarget = file->readSint32();
	s.speed = file->readSint32();
	for (int i = 0; i < 3; i++)
		s.turnRate[i] = file->readSint32();
	for (int i = 0; i < 3; i++)
		s.turned[i] = file->readSint32();
	s.timer = file->readSint16();
	// 0xa8: to the ship's 0x1bc
	s.closeRange = file->readSint32();
	s.canCloak = file->readSint16();
	// 0xae: the weapons (to the ship's 0x15c)
	for (int i = 0; i < 4; i++) {
		ShipWeapon &w = s.weapons[i];
		w.nextFire = file->readUint32();
		w.life = file->readSint16();
		w.speed = file->readSint16();
		w.interval = file->readSint16();
		for (int j = 0; j < 3; j++)
			w.offset[j] = file->readSint32();
		w.kind = file->readSint16();
	}
	delete file;
	return true;
}

// An enemy ship near the Enterprise (2f0d:0e49)
R3 *StarTrekEngine::spawnShip(const Common::String &name) {
	R3 *r3 = nullptr;
	for (int i = 0; i < NUM_ENEMY_SHIPS; i++) {
		if (_enemyShips[i].field1e == 0) {
			r3 = &_enemyShips[i];
			break;
		}
	}
	if (!r3)
		return nullptr;
	*r3 = R3();
	r3->pos = _enterpriseR3.pos;
	for (int i = 0; i < 3; i++)
		r3->pos[i] += (0x40 - (combatRand() & 0x7f)) * 0x10;
	if (!loadShipClass(r3, name)) {
		warning("spawnShip: no %s.DSC", name.c_str());
		return nullptr;
	}
	r3->matrix = initMatrix();
	r3->field1e = 2;
	r3->shipViews = true;
	ShipState &s = r3->ship;
	s.shieldsRequested = 1;
	s.weaponsRequested = 1;
	s.cloakFading = 1;
	s.leaving = 0;
	addR3(r3);
	return r3;
}

// Power (2f0d:109d): what the generators give, to the shields, the cloak,
// the weapons and the engines in that order
void StarTrekEngine::shipPower(R3 *r3) {
	ShipState &s = r3->ship;
	bool isEnterprise = r3 == &_enterpriseR3;
	s.power = s.systems[kSystemPower0].cur + s.systems[kSystemPower0 + 1].cur + s.systems[kSystemPower0 + 2].cur;
	// (Scotty giving her all she's got, 2f0d:30ef)
	if (isEnterprise && _extraPowerTicks != 0) {
		s.power += _extraPower;
		_extraPowerTicks--;
	}

	if (s.shieldsRequested == 0) {
		s.shieldsUp = 0;
	} else if (s.power < 10) {
		if (s.shieldsUp != 0) {
			addSpaceEvent(kSpaceEventShieldsDown, !isEnterprise);
			s.shieldsUp = 0;
		}
	} else {
		s.power -= 10;
		s.shieldsUp = 1;
	}

	if (s.cloakRequested == 0) {
		s.cloaked = 0;
	} else if (s.power < 0x14) {
		s.cloaked = 0;
	} else {
		if (s.cloaked == 0)
			s.cloakFading = 0;
		s.power -= 0x14;
		s.cloaked = 1;
	}

	if (s.weaponsRequested == 0) {
		s.weaponsArmed = 0;
	} else if (s.power < 10) {
		if (s.weaponsArmed != 0) {
			addSpaceEvent(kSpaceEventWeaponsDown, !isEnterprise);
			s.weaponsArmed = 0;
		}
	} else {
		s.power -= 10;
		s.weaponsArmed = 1;
	}

	// the engines: up to 70, for the requested speed
	int16 avail = MIN<int16>(s.power, 0x46);
	int16 needed = s.maxSpeed == 0 ? 0 : (int16)(((int64)s.requestedSpeed * 0x46) / s.maxSpeed);
	if (avail < needed) {
		s.speedTarget = (int32)(((int64)s.maxSpeed * avail) / 0x46);
		s.power -= avail;
	} else {
		s.speedTarget = s.requestedSpeed;
		s.power -= needed;
	}
}

// The speed towards its target, and the velocity along the ship's heading
// (2f0d:0d22)
static void updateVelocity(R3 *r3, int32 speed) {
	ShipState &s = r3->ship;
	if (r3->field1e == 2) {
		int32 d = speed - s.speed;
		if (d <= -s.speedAccel)
			d = -s.speedAccel;
		if (d > s.speedAccel)
			d = s.speedAccel;
		s.speed += d;
		speed = s.speed;
	}
	int16 v = (int16)(speed >> 16);
	r3->speed.x = fx14Mul(v, r3->matrix[2][0].raw());
	r3->speed.y = fx14Mul(v, r3->matrix[2][1].raw());
	r3->speed.z = fx14Mul(v, r3->matrix[2][2].raw());
}

// Flight (2f0d:1bd5): shots fly straight; ships turn towards their turn
// rates as fast as the helm lets them, turn, speed up or slow, and move
void StarTrekEngine::moveShip(R3 *r3) {
	if (r3->field1e != 3) {
		ShipState &s = r3->ship;
		const ShipSystem &helm = s.systems[kSystemHelm];
		int32 health = helm.max == 0 ? 0 : (int32)(((int32)helm.cur << 16) / helm.max);

		for (int axis = 0; axis < 3; axis++) {
			int32 d = s.targetTurnRate[axis] - s.turnRate[axis];
			if (d == 0)
				continue;
			if (d > s.turnAccel)
				d = s.turnAccel;
			if (d < -s.turnAccel)
				d = -s.turnAccel;
			d = (int32)((int64)lshr(d, 16) * health);
			s.turnRate[axis] += d;
			if (s.turnRate[axis] <= -s.maxTurnRate)
				s.turnRate[axis] = -s.maxTurnRate;
			if (s.turnRate[axis] >= s.maxTurnRate)
				s.turnRate[axis] = s.maxTurnRate;
		}

		const CombatTables &t = combatTables();
		for (int axis = 0; axis < 3; axis++) {
			int32 a = lshr(s.turnRate[axis], 16);
			if (a == 0)
				continue;
			int i = MIN<int32>(ABS(a), 0x20);
			int16 sn = a < 0 ? -t.turnSin[i] : t.turnSin[i];
			turnMatrix(r3->matrix, axis, sn, t.turnCos[i]);
			s.turned[axis] += t.turnAngle[i];
		}

		s.timer--;
		updateVelocity(r3, s.speedTarget);
	}
	r3->pos.x += r3->speed.x;
	r3->pos.y += r3->speed.y;
	r3->pos.z += r3->speed.z;
}

// Repairs (2f0d:195d), every 32 ticks: each shield and system a point, and
// on the Enterprise what Scotty was told to repair another
void StarTrekEngine::repairShip(R3 *r3) {
	if ((_frameIndex & 0x1f) != 7)
		return;
	ShipState &s = r3->ship;
	for (int i = 0; i < 6; i++)
		s.shields[i].cur = MIN<int16>(s.shields[i].cur + 1, s.shields[i].max);
	for (int i = 0; i < kNumShipSystems; i++)
		s.systems[i].cur = MIN<int16>(s.systems[i].cur + 1, s.systems[i].max);
	if (r3 != &_enterpriseR3)
		return;

	// (2f0d:1a45..1b96)
	ShipSystem *group;
	int count;
	switch (_repairTarget) {
	case 0:
	case 1:
	case 6: {
		ShipSystem &sys = s.systems[_repairTarget];
		if (sys.cur == sys.max) {
			addSpaceEvent(kSpaceEventRepaired, _repairTarget);
			_repairTarget = -1;
		} else {
			sys.cur = MIN<int16>(sys.cur + 1, sys.max);
		}
		return;
	}
	case 2:
		group = &s.systems[2];
		count = 2;
		break;
	case 4:
		group = &s.systems[4];
		count = 2;
		break;
	case 7:
		group = &s.systems[7];
		count = 3;
		break;
	case 0x7fff:
		group = s.shields;
		count = 6;
		break;
	default:
		return;
	}
	int unrepaired = count;
	for (int i = 0; i < count; i++) {
		if (group[i].cur == group[i].max)
			unrepaired--;
		else
			group[i].cur = MIN<int16>(group[i].cur + 1, group[i].max);
	}
	if (unrepaired == 0) {
		addSpaceEvent(kSpaceEventRepaired, _repairTarget);
		_repairTarget = -1;
	}
}

// Fire one of a ship's weapons along dir (or straight ahead) (2f0d:000a).
// The mount (its system) must be at least half working.
bool StarTrekEngine::fireWeapon(R3 *r3, int weapon, const Point3 *dir) {
	ShipState &s = r3->ship;
	ShipWeapon &w = s.weapons[weapon];
	const ShipSystem &mount = s.systems[kSystemWeapon0 + weapon];
	if ((mount.max >> 1) > mount.cur)
		return false;
	if (w.nextFire > _frameIndex || mount.enabled == 0 || s.weaponsArmed == 0)
		return false;

	R3 *shot = nullptr;
	for (int i = 0; i < NUM_SHOTS; i++) {
		if (_shots[i].field1e == 0) {
			shot = &_shots[i];
			break;
		}
	}
	if (!shot)
		return false;
	*shot = R3();
	shot->pos = r3->pos + fromShipAxes(w.offset, r3->matrix);
	if (dir == nullptr) {
		shot->matrix = r3->matrix;
	} else {
		// only its heading
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				shot->matrix[i][j] = Fixed14::fromRaw(0);
		int16 len = vectorLength(*dir);
		if (len == 0)
			len = 1;
		for (int i = 0; i < 3; i++)
			shot->matrix[2][i] = Fixed14::fromRaw(fx14Div((int16)(*dir)[i], len));
	}
	shot->field1e = 3;
	shot->field20 = w.kind;
	shot->field22 = 0x3c;
	shot->field24 = 3;
	updateVelocity(shot, ((int32)w.speed << 16) + s.speed);
	shot->owner = r3;
	shot->life = w.life;
	shot->damage = mount.max;
	switch (r3->field20) {
	case 0:
		shot->color = 0xa0;
		break;
	case 1:
		shot->color = 0xb8;
		break;
	case 2:
		shot->color = 0xea;
		break;
	default:
		break;
	}
	w.nextFire = _frameIndex + w.interval;
	addR3(shot);
	return true;
}

// Whether a point is within a ship's radius, a cube (2f0d:09b6)
static bool insideShip(const Point3 &p, const R3 *ship) {
	int32 r = ship->field22;
	for (int i = 0; i < 3; i++) {
		int32 d = p[i] - ship->pos[i];
		if (d < -r || d > r)
			return false;
	}
	return true;
}

// The side of a ship facing a point in its axes (2f0d:1442): 0 right,
// 2 left, 4 above, 5 below, 3 ahead, 1 behind
static int findShipSide(const Point3 &v) {
	int32 x = ABS(v.x), y = ABS(v.y), z = ABS(v.z);
	if (x >= y && x >= z)
		return v.x < 0 ? 2 : 0;
	if (y >= x && y >= z)
		return v.y < 0 ? 5 : 4;
	return v.z < 0 ? 1 : 3;
}

// Which system a hit on each side damages, by a roll of 0..99: (lowest,
// highest, system) (DS:1192, 119e)
struct DamageRoll {
	int16 lo, hi, system;
};

static const DamageRoll kDamageRight[] = {
	{ 0, 19, 9 }, { 20, 24, 8 }, { 25, 34, 7 }, { 35, 39, 1 }, { 40, 49, 6 },
	{ 50, 51, 4 }, { 52, 54, 5 }, { 55, 56, 2 }, { 57, 59, 3 }, { 60, 99, 0 }
};
static const DamageRoll kDamageBehind[] = {
	{ 0, 24, 9 }, { 25, 49, 8 }, { 50, 64, 7 }, { 65, 99, 0 }
};
static const DamageRoll kDamageLeft[] = {
	{ 0, 19, 8 }, { 20, 24, 9 }, { 25, 34, 7 }, { 35, 39, 1 }, { 40, 49, 6 },
	{ 50, 51, 5 }, { 52, 54, 4 }, { 55, 56, 3 }, { 57, 59, 2 }, { 60, 99, 0 }
};
static const DamageRoll kDamageAhead[] = {
	{ 0, 9, 8 }, { 10, 19, 9 }, { 20, 24, 1 }, { 25, 39, 6 }, { 40, 44, 5 },
	{ 45, 49, 4 }, { 50, 54, 3 }, { 55, 59, 2 }, { 60, 99, 0 }
};
static const DamageRoll kDamageAbove[] = {
	{ 0, 19, 8 }, { 20, 39, 9 }, { 40, 49, 7 }, { 50, 54, 1 }, { 55, 56, 3 },
	{ 57, 59, 2 }, { 60, 99, 0 }
};
static const DamageRoll kDamageBelow[] = {
	{ 0, 14, 8 }, { 15, 29, 9 }, { 30, 34, 7 }, { 35, 44, 6 }, { 45, 49, 5 },
	{ 50, 54, 4 }, { 55, 57, 3 }, { 58, 59, 2 }, { 60, 99, 0 }
};

static const struct {
	const DamageRoll *rolls;
	int count;
} kDamageTables[6] = {
	{ kDamageRight, ARRAYSIZE(kDamageRight) },
	{ kDamageBehind, ARRAYSIZE(kDamageBehind) },
	{ kDamageLeft, ARRAYSIZE(kDamageLeft) },
	{ kDamageAhead, ARRAYSIZE(kDamageAhead) },
	{ kDamageAbove, ARRAYSIZE(kDamageAbove) },
	{ kDamageBelow, ARRAYSIZE(kDamageBelow) }
};

// A system takes the damage, the hull what it can't (2f0d:1807). True when
// the hull gives (or, as the original, when the roll finds no system).
bool StarTrekEngine::damageSystem(R3 *r3, int side, int16 damage) {
	int roll = combatRand() % 100;
	const DamageRoll *rolls = kDamageTables[side].rolls;
	int count = kDamageTables[side].count;
	int i;
	for (i = 0; i < count; i++) {
		if (rolls[i].lo <= roll && roll <= rolls[i].hi)
			break;
	}
	if (i == count)
		return count == 1;
	ShipState &s = r3->ship;
	ShipSystem &sys = s.systems[rolls[i].system];
	int16 absorbed = MIN(sys.cur, damage);
	sys.cur -= absorbed;
	damage -= absorbed;
	if (damage == 0)
		return false;
	s.systems[kSystemHull].cur -= damage;
	return s.systems[kSystemHull].cur <= 0;
}

// A shot hits a ship (2f0d:12e4): the shield on that side takes what it
// can, the rest damages a system
void StarTrekEngine::hitShip(R3 *r3, R3 *shot) {
	ShipState &s = r3->ship;
	Point3 local = toShipAxes(shot->pos - r3->pos, r3->matrix);
	int side = findShipSide(local);
	if (s.shieldsUp != 0) {
		ShipSystem &shield = s.shields[side];
		int16 strength = shield.cur;
		shield.cur -= MIN(shot->damage, strength);
		if (shield.max != 0)
			shot->damage -= (shot->damage * strength) / shield.max;
	}
	if (damageSystem(r3, side, shot->damage) && s.destroyed == 0) {
		if (r3 == &_enterpriseR3)
			_sound->playMidiMusicTracks((MidiTracks)4);
		else if (_sound->getLoopingMidiTrack() != 3)
			_sound->playMidiMusicTracks((MidiTracks)3, kLoopTypeLast);
		s.destroyed = 1;
		s.explosionTimer = -1;
		s.cloakRequested = 0;
		s.cloaked = 0;
	}
}

// A hit shot becomes its burst, moving with the ship (2f0d:1029)
static void burstShot(R3 *shot, const R3 *ship) {
	shot->field20 = 3;
	shot->life = 8;
	shot->field22 = 0x3c;
	shot->field24 = 6;
	shot->matrix = ship->matrix;
	shot->speed = ship->speed;
}

// The shots (2f0d:02a8): spent ones go; the others hit the first ship of
// the other side they're inside, then fly on
void StarTrekEngine::moveShots() {
	for (int i = 0; i < NUM_SHOTS; i++) {
		R3 *shot = &_shots[i];
		if (shot->field1e != 3)
			continue;
		if (shot->life-- < 1) {
			delR3(shot);
			continue;
		}
		if (shot->field20 != 3) {
			for (int j = 0; j < NUM_SPACE_OBJECTS; j++) {
				R3 *ship = _r3List[j];
				if (!ship || ship->field1e != 2 || ship == shot->owner)
					continue;
				if (!insideShip(shot->pos, ship))
					continue;
				bool shipIsEnterprise = ship == &_enterpriseR3;
				bool ownerIsEnterprise = shot->owner == &_enterpriseR3;
				if (shipIsEnterprise != ownerIsEnterprise) {
					burstShot(shot, ship);
					hitShip(ship, shot);
					if (!shipIsEnterprise)
						_targetAnalysisShip = ship;
				}
				break;
			}
		}
		moveShip(shot);
	}
}

// An enemy fires everything it has when the Enterprise is in a narrow cone
// ahead (2f0d:2f68)
void StarTrekEngine::enemyFire(R3 *r3, const Point3 &local, const Point3 &toEnterprise) {
	int32 side = MAX(ABS(local.x), ABS(local.y));
	if ((side << 2) >= local.z)
		return;
	bool fired = false;
	for (int i = 0; i < 4; i++)
		fired |= fireWeapon(r3, i, &toEnterprise);
	if (fired)
		_sound->playSoundEffectIndex(kSfxPhotonTorpedoes);
}

// Close in on the Enterprise: turn towards it, slower when near
// (part of 2f0d:0480)
static void steer(R3 *r3, const Point3 &local, int32 distance, int32 speed, bool away) {
	ShipState &s = r3->ship;
	int16 pitch = tableAsin(fx14Div((int16)local.y, (int16)distance));
	int16 yaw = tableAtan2(local.x, local.z);
	if (away) {
		s.requestedSpeed = s.maxSpeed;
		s.targetTurnRate[0] = (int32)pitch << 16;
		s.targetTurnRate[1] = (int32)(int16)-yaw << 16;
	} else {
		s.requestedSpeed = speed;
		s.targetTurnRate[0] = (int32)(int16)-pitch << 16;
		s.targetTurnRate[1] = (int32)yaw << 16;
	}
	s.targetTurnRate[2] = 0;
}

static int32 randomSign(int r) {
	return (r & 1) == 0 ? 1 : -1;
}

// The maneuvers when close (DS:1060): 0 a loop, 1 a zigzag, 2 random
// turns. False when it's done this tick without moving.
bool StarTrekEngine::shipManeuver(R3 *r3) {
	ShipState &s = r3->ship;
	switch (s.maneuver) {
	case 0: // (2f0d:2781) a loop, half way and on round if it isn't away
		if (s.maneuverStep == 0) {
			s.requestedSpeed = s.maxSpeed;
			s.targetTurnRate[0] = s.maxTurnRate * randomSign(combatRand());
			s.targetTurnRate[1] = 0;
			s.targetTurnRate[2] = 0;
			s.turned[0] = 0;
			s.maneuverStep++;
		} else if (s.maneuverStep == 1) {
			if ((s.turned[0] >> 16) > 0x1ff) {
				if (s.closing > 0)
					s.maneuvering = 0;
				s.maneuverStep++;
				return false;
			}
		} else if (s.maneuverStep == 2 && (s.turned[0] >> 16) > 0x3ff) {
			s.maneuvering = 0;
			return false;
		}
		break;

	case 1: // (2f0d:28bf) up, round, up, up
		switch (s.maneuverStep) {
		case 0:
			s.requestedSpeed = s.maxSpeed;
			s.targetTurnRate[0] = s.targetTurnRate[1] = s.targetTurnRate[2] = 0;
			s.timer = 0x20;
			s.maneuverStep++;
			break;
		case 1:
		case 5:
		case 7:
			if (s.timer < 0) {
				s.targetTurnRate[0] = -s.maxTurnRate;
				s.turned[0] = 0;
				s.maneuverStep++;
			}
			break;
		case 2:
		case 6:
			if ((s.turned[0] >> 16) > 0xff) {
				s.targetTurnRate[0] = 0;
				s.timer = 0x30;
				s.maneuverStep++;
			}
			break;
		case 3:
			if (s.timer < 0) {
				s.targetTurnRate[1] = s.maxTurnRate * randomSign(combatRand());
				s.turned[1] = 0;
				s.maneuverStep++;
			}
			break;
		case 4:
			if ((s.turned[1] >> 16) > 0x1ff) {
				s.targetTurnRate[1] = 0;
				s.timer = 0x30;
				s.maneuverStep++;
			}
			break;
		case 8:
			s.maneuvering = 0;
			return false;
		default:
			break;
		}
		break;

	default: { // (2f0d:2b51) turns about a random axis, until well away
		int step = s.maneuverStep;
		if ((step & 1) == 0 && step < 8) {
			if (s.closing > 0 && s.distance > s.closeRange) {
				s.maneuvering = 0;
				break;
			}
			s.requestedSpeed = s.maxSpeed;
			if (step == 0) {
				s.targetTurnRate[0] = s.targetTurnRate[1] = s.targetTurnRate[2] = 0;
				s.timer = combatRand() & 0x3f;
			} else if (step == 2) {
				s.targetTurnRate[0] = s.maxTurnRate * randomSign(combatRand());
				s.targetTurnRate[1] = s.targetTurnRate[2] = 0;
				s.turned[0] = 0;
			} else if (step == 4) {
				s.targetTurnRate[0] = 0;
				s.targetTurnRate[1] = s.maxTurnRate * randomSign(combatRand());
				s.targetTurnRate[2] = 0;
				s.turned[1] = 0;
			} else {
				s.targetTurnRate[1] = 0;
				s.targetTurnRate[2] = s.maxTurnRate * randomSign(combatRand());
				s.turned[2] = 0;
			}
			s.maneuverStep++;
		} else if (step == 1) {
			if (s.timer < 0)
				s.maneuverStep = (combatRand() & 3) << 1;
		} else if (step < 8) {
			int axis = step >> 1;
			if ((s.turned[axis - 1] >> 16) > 0xff)
				s.maneuverStep = (combatRand() & 3) << 1;
		}
		break;
	}
	}
	return true;
}

// An enemy's tick (2f0d:0480)
void StarTrekEngine::enemyShipTick(R3 *r3) {
	ShipState &s = r3->ship;
	if (s.destroyed != 0) {
		moveShip(r3);
		return;
	}
	shipPower(r3);
	repairShip(r3);
	if (s.canCloak != 0)
		s.cloakRequested = 1;

	// (its sensors: the worse they are, the more ticks it does nothing)
	const ShipSystem &sensors = s.systems[kSystemSensors];
	int16 range = (sensors.max * 3) >> 2;
	if (range == 0 || (combatRand() % range) > sensors.cur) {
		moveShip(r3);
		return;
	}

	Point3 toEnterprise = _enterpriseR3.pos - r3->pos;
	Point3 local = toShipAxes(toEnterprise, r3->matrix);
	int32 distance = approxDistance(toEnterprise);
	s.closing = distance - s.distance;
	s.distance = distance;

	if (s.canCloak != 0) {
		// decloak to fire when the Enterprise is ahead
		if ((MAX(ABS(local.x), ABS(local.y)) << 2) < local.z)
			s.cloakRequested = 0;
		if (s.cloakFading == 0)
			enemyFire(r3, local, toEnterprise);
	} else {
		enemyFire(r3, local, toEnterprise);
	}

	if (s.leaving == 1) {
		if (s.distance > 0x2ee0) {
			delR3(r3);
			r3->field1e = 0;
			return;
		}
		steer(r3, local, s.distance, 0, true);
		moveShip(r3);
		return;
	}

	int32 speed;
	if ((s.requestedSpeed >> 2) > s.speedTarget) {
		// (the engines can't give a quarter of what it asks)
		speed = 0;
		s.maneuvering = 0;
	} else {
		if (s.maneuvering != 0) {
			if (shipManeuver(r3))
				moveShip(r3);
			return;
		}
		if (s.distance <= s.closeRange) {
			s.maneuvering = 1;
			s.maneuver = combatRand() % 3;
			s.maneuverStep = 0;
			return;
		}
		if (s.distance > s.closeRange * 3)
			speed = s.maxSpeed;
		else if (s.distance > s.closeRange << 1)
			speed = s.maxSpeed >> 1;
		else
			speed = s.maxSpeed >> 2;
	}
	steer(r3, local, s.distance, speed, false);
	moveShip(r3);
}

// The enemies (2f0d:043c); whether there are any
void StarTrekEngine::enemyShipsTick() {
	_inCombat = false;
	for (int i = 0; i < NUM_ENEMY_SHIPS; i++) {
		if (_enemyShips[i].field1e != 0) {
			enemyShipTick(&_enemyShips[i]);
			_inCombat = true;
		}
	}
}

// The shots go when leaving combat (2f0d:03f3)
void StarTrekEngine::removeShots() {
	for (int i = 0; i < NUM_SHOTS; i++) {
		if (_shots[i].field1e == 3)
			delR3(&_shots[i]);
	}
}

// Scotty's extra power (2f0d:30ef): false if he's giving it already
bool StarTrekEngine::boostPower() {
	if (_extraPowerUsed)
		return false;
	_extraPowerUsed = true;
	_extraPowerTicks = _extraPowerDuration;
	_extraPower = _extraPowerBonus;
	return true;
}

// ---- Drawing them (segment 1972) ----

// The shapes combat shows (shots, bursts, explosions), kept once loaded
Bitmap *StarTrekEngine::combatShape(const Common::String &name) {
	Bitmap *&bitmap = _combatShapes[name];
	if (!bitmap) {
		Common::MemoryReadStreamEndian *file = _resource->loadFile(name + ".SHP", 0, false);
		if (!file)
			return nullptr;
		bitmap = new Bitmap(file, true);
	}
	return bitmap;
}

void StarTrekEngine::freeCombatShapes() {
	for (auto &i : _combatShapes)
		delete i._value;
	_combatShapes.clear();
}

// A shot, a burst or a mission's object, by its field20 (1972:097b)
void StarTrekEngine::drawShot(R3 *r3) {
	Common::String name;
	switch (r3->field20) {
	case 0: // photon torpedo
		name = Common::String::format("shot%d", 1 + (r3->life & 1));
		break;
	case 2: // Romulan plasma, growing
		name = Common::String::format("plasm%d", _frameIndex & 3);
		if ((_frameIndex & 3) == 0)
			r3->field24++;
		break;
	case 3: // a hit
		name = Common::String::format("expl0%d", r3->life & 3);
		if (r3->life == 5)
			_sound->playSoundEffectIndex(kSfxShieldHit);
		break;
	case 4:
		name = "ark7";
		break;
	case 5:
		name = "derelict";
		break;
	case 6:
		name = "masada";
		break;
	case 7:
		name = "republic";
		break;
	default:
		return;
	}
	r3->bitmap = combatShape(name);
	if (r3->bitmap)
		drawR3Shape(r3);
	r3->bitmap = nullptr;
}

// An exploding ship's explosion, drawn as a planet-like shape (1972:04dd,
// 0b83 with field20 4)
void StarTrekEngine::drawShipExplosion(R3 *r3) {
	Bitmap *view = r3->bitmap;
	int16 type = r3->field1e, cls = r3->field20, size = r3->field24;
	r3->field1e = 4;
	r3->field20 = 4;
	r3->field24 = r3->ship.explosionTimer < 0x18 ? 6 : 12;
	r3->bitmap = combatShape(Common::String::format("BigExp%02d", r3->ship.explosionTimer));
	if (r3->bitmap)
		drawR3Shape(r3);
	r3->bitmap = view;
	r3->field1e = type;
	r3->field20 = cls;
	r3->field24 = size;
}

// The cloak's palette remaps (CLOAK0..8.LUT), 1da2:04d7
const byte *StarTrekEngine::cloakTable(int level) {
	if (level != _cloakLevel) {
		Common::MemoryReadStreamEndian *file = _resource->loadFile(Common::String::format("cloak%d.lut", level), 0, false);
		if (!file)
			return nullptr;
		file->read(_cloakTable, 256);
		delete file;
		_cloakLevel = level;
	}
	return _cloakTable;
}

// The space view's area, inclusive (DS:d6a2..d6a8)
static void spaceViewport(const Common::Rect &r, int16 &left, int16 &top, int16 &right, int16 &bottom) {
	left = r.left;
	top = r.top;
	right = r.right - 1;
	bottom = r.bottom - 1;
}

// A row's span: right (max x) and left (min x)
struct BeamSpan {
	int16 right, left;
};

struct BeamSpans {
	Common::Array<BeamSpan> rows;
	int16 top, bottom;

	void update(int y, int xl, int xr) {
		if (y < top || y > bottom)
			return;
		BeamSpan &s = rows[y - top];
		s.left = MIN<int16>(s.left, xl);
		s.right = MAX<int16>(s.right, xr);
	}

	// (1972:2e7c) a midpoint circle
	void circle(int cx, int cy, int r) {
		int x = 0, y = r, d = 3 - 2 * r;
		for (; x < y; x++) {
			update(cy + y, cx - x, cx + x);
			update(cy - y, cx - x, cx + x);
			update(cy + x, cx - y, cx + y);
			update(cy - x, cx - y, cx + y);
			if (d < 0) {
				d += 4 * x + 6;
			} else {
				d += 4 * (x - y) + 10;
				y--;
			}
		}
		if (x == y) {
			update(cy + y, cx - x, cx + x);
			update(cy - y, cx - x, cx + x);
			update(cy + x, cx - y, cx + y);
			update(cy - x, cx - y, cx + y);
		}
	}

	// (1972:3bbb) the points of a line, both ends
	void edge(int x1, int y1, int x2, int y2) {
		int dx = ABS(x2 - x1), dy = ABS(y2 - y1);
		int sx = x2 >= x1 ? 1 : -1, sy = y2 >= y1 ? 1 : -1;
		int x = x1, y = y1;
		if (dx < dy) {
			int e = 2 * dx - dy;
			for (int i = 0; i <= dy; i++) {
				update(y, x, x);
				if (e < 0) {
					e += 2 * dx;
				} else {
					x += sx;
					e += 2 * (dx - dy);
				}
				y += sy;
			}
		} else {
			int e = 2 * dy - dx;
			for (int i = 0; i <= dx; i++) {
				update(y, x, x);
				if (e < 0) {
					e += 2 * dy;
				} else {
					y += sy;
					e += 2 * (dy - dx);
				}
				x += sx;
			}
		}
	}
};

// A phaser shot: a flat capsule from 200 behind it to 200 ahead, with a
// core line and darker edges (1972:2b2c)
void StarTrekEngine::drawPhaserBeam(R3 *r3) {
	Point3 a = r3->field36, b = r3->field36;
	for (int i = 0; i < 3; i++) {
		int32 d = ((int32)r3->matrix2[2][i].raw() * 200) / 0x4000;
		b[i] -= d;
		a[i] += d;
	}
	if (a.z < 50 || b.z < 50)
		return;
	int16 left, top, right, bottom;
	spaceViewport(_starfieldRect, left, top, right, bottom);

	int16 rB = CLIP<int32>(scaleSpacePosition(r3->field22 >> 3, b.z), 1, 99);
	int16 rA = CLIP<int32>(scaleSpacePosition(r3->field22 >> 3, a.z), 1, 99);
	int16 bx = scaleSpacePosition(b.x, b.z) + _starfieldXVar1;
	int16 by = _starfieldYVar1 - scaleSpacePosition(b.y, b.z);
	int16 ax = scaleSpacePosition(a.x, a.z) + _starfieldXVar1;
	int16 ay = _starfieldYVar1 - scaleSpacePosition(a.y, a.z);

	BeamSpans spans;
	spans.top = top;
	spans.bottom = bottom;
	BeamSpan empty = { (int16)-0x7fff, 0x7fff };
	spans.rows.resize(bottom - top + 1);
	for (uint i = 0; i < spans.rows.size(); i++)
		spans.rows[i] = empty;

	spans.circle(ax, ay, rA);
	spans.circle(bx, by, rB);
	// (1972:3410) the hull between the two circles
	{
		int16 angle = trekAtan2(ay - by, ax - bx);
		int r1 = rB + 1, r2 = rA + 1;
		int16 cp = trekCos(angle + 0x100), sp = trekSin(angle + 0x100);
		int16 cm = trekCos(angle - 0x100), sm = trekSin(angle - 0x100);
		int p1x = bx + (r1 * cp) / 0x4000, p1y = by + (r1 * sp) / 0x4000;
		int p2x = ax + (r2 * cp) / 0x4000, p2y = ay + (r2 * sp) / 0x4000;
		int p3x = ax + (r2 * cm) / 0x4000, p3y = ay + (r2 * sm) / 0x4000;
		int p4x = bx + (r1 * cm) / 0x4000, p4y = by + (r1 * sm) / 0x4000;
		spans.edge(p3x, p3y, p4x, p4y);
		spans.edge(p1x, p1y, p2x, p2y);
	}

	byte *pixels = _gfx->getBackgroundPixels();
	byte color = (byte)r3->color;
	// (1972:3f65) the body
	for (int y = top; y <= bottom; y++) {
		const BeamSpan &s = spans.rows[y - top];
		if (s.left > s.right || s.left > right || s.right < left)
			continue;
		int l = MAX<int>(s.left, left), r = MIN<int>(s.right, right);
		memset(pixels + y * SCREEN_WIDTH + l, color + 2, r - l + 1);
	}
	// (1972:3685) the core: a line, clipped, its last point left out
	{
		int dx = ABS(ax - bx), dy = ABS(ay - by);
		int sx = ax >= bx ? 1 : -1, sy = ay >= by ? 1 : -1;
		int count = MAX(dx, dy);
		int x = bx, y = by;
		int e = dx >= dy ? 2 * dy - dx : 2 * dx - dy;
		for (int i = 0; i < count; i++) {
			if (x >= left && x <= right && y >= top && y <= bottom)
				pixels[y * SCREEN_WIDTH + x] = color;
			if (dx >= dy) {
				if (e < 0) {
					e += 2 * dy;
				} else {
					y += sy;
					e += 2 * (dy - dx);
				}
				x += sx;
			} else {
				if (e < 0) {
					e += 2 * dx;
				} else {
					x += sx;
					e += 2 * (dx - dy);
				}
				y += sy;
			}
		}
	}
	// (1972:4005) its edges
	for (int y = top; y <= bottom; y++) {
		const BeamSpan &s = spans.rows[y - top];
		if (s.left > s.right || s.left > right || s.right < left)
			continue;
		if (s.left >= left)
			pixels[y * SCREEN_WIDTH + s.left] = color + 4;
		if (s.right <= right)
			pixels[y * SCREEN_WIDTH + s.right] = color + 4;
	}
}

// Static over the view, a random tile of STATIC0..4 (1972:019b)
void StarTrekEngine::drawViewscreenStatic() {
	Bitmap *tile = new Bitmap(_resource->loadBitmapFile(Common::String::format("static%d", combatRand() % 5)));
	int16 left, top, right, bottom;
	spaceViewport(_starfieldRect, left, top, right, bottom);
	int w = right - left + 1, h = bottom - top + 1;
	byte *pixels = _gfx->getBackgroundPixels();
	for (int ty = 0; ty < tile->height; ty++) {
		for (int tx = 0; tx < tile->width; tx++) {
			byte c = tile->pixels[ty * tile->width + tx];
			if (!c)
				continue;
			for (int y = ty; y < h; y += tile->height)
				for (int x = tx; x < w; x += tile->width)
					pixels[(top + y) * SCREEN_WIDTH + left + x] = c;
		}
	}
	delete tile;
}

// The sensors damaged, static (11dc:1c82's argument to 1972:04dd)
bool StarTrekEngine::viewscreenStatic() {
	const ShipSystem &sensors = _enterpriseR3.ship.systems[kSystemSensors];
	return sensors.max != 0 && sensors.cur < combatRand() % sensors.max;
}

// The matrices drift as they're turned: straightened every 64 ticks
// (1972:2acd, 2407:1097)
static void orthonormalize(Matrix &m) {
	int16 r[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r[i][j] = m[i][j].raw();
	auto cross = [](const int16 *a, const int16 *b, int16 *out) {
		out[0] = (int16)(((int32)a[1] * b[2] + (int32)a[2] * -b[1]) >> 14);
		out[1] = (int16)(((int32)a[2] * b[0] + (int32)a[0] * -b[2]) >> 14);
		out[2] = (int16)(((int32)a[0] * b[1] + (int32)a[1] * -b[0]) >> 14);
	};
	cross(r[1], r[2], r[0]);
	cross(r[2], r[0], r[1]);
	for (int i = 0; i < 3; i++) {
		int32 len = (int32)sqrt((double)((int32)r[i][0] * r[i][0] + (int32)r[i][1] * r[i][1] + (int32)r[i][2] * r[i][2]));
		for (int j = 0; j < 3; j++)
			m[i][j] = Fixed14::fromRaw(len == 0 ? 0 : (int16)(((int32)r[i][j] << 14) / len));
	}
}

// ---- The captain's side (segment 11dc) ----

// The simulation's news, on the bridge's queue (1e8e:17b9)
void StarTrekEngine::addSpaceEvent(int type, int data) {
	postBridgeAction(type, data);
}

// From Scotty (11dc:1c82 cases 2..7)
void StarTrekEngine::handleSpaceEvent(int type, int data) {
	static const struct {
		int16 id;
		const char *name;
	} kRepairNames[] = {
		{ 0xff, "#BRID\\SCOTSHIE#Shields" },
		{ 0, "#BRID\\SCOTHULL#Hull" },
		{ 1, "#BRID\\SCOTBRID#Bridge" },
		{ 6, "#BRID\\SCOTSENS#Sensors" },
		{ 2, "#BRID\\SCOTTORP#Torpedoes" },
		{ 4, "#BRID\\SCOTPHAS#Phasers" },
		{ 7, "#BRID\\SCOTENGI#Engines" }
	};

	bool fromEnterprise = data == 0 && _enterpriseR3.ship.destroyed == 0;
	switch (type) {
	case kSpaceEventShieldsDown:
		if (!fromEnterprise)
			break;
		showTextboxBridge(kBridgeTalkerScotty, "#BRID\\BRID_S45#The shields are falling!");
		// (the original goes on to the weapons' message too)
		// fall through
	case kSpaceEventWeaponsDown:
		if (fromEnterprise)
			showTextboxBridge(kBridgeTalkerScotty, "#BRID\\BRID_S37#Our weapons are off-line!");
		break;
	case kSpaceEventRepaired:
		for (int i = 0; i < ARRAYSIZE(kRepairNames); i++) {
			if (kRepairNames[i].id == (data & 0xff)) {
				showTextboxBridge(kBridgeTalkerScotty, Common::String::format("%s repaired, Captain.", kRepairNames[i].name));
				break;
			}
		}
		break;
	default:
		break;
	}
}

// Steering: the cursor's offset from the view's centre is the turn rate
// wanted (11dc:042c, mouse moves in ship mode)
void StarTrekEngine::steerEnterprise(Common::Point mouse) {
	if (_enterpriseState.inOrbit || _enterpriseR3.ship.destroyed)
		return;
	ShipState &s = _enterpriseR3.ship;
	s.targetTurnRate[1] = (int32)(int16)(mouse.x - _starfieldXVar1) << 16;
	s.targetTurnRate[0] = (int32)(int16)(mouse.y - _starfieldYVar1) << 16;
}

// Keypad 5: straight on (11dc:1795)
void StarTrekEngine::centerEnterpriseControls() {
	ShipState &s = _enterpriseR3.ship;
	s.targetTurnRate[0] = s.targetTurnRate[1] = s.targetTurnRate[2] = 0;
	_system->warpMouse(_starfieldXVar1, _starfieldYVar1);
}

// The speed keys: '1' stop .. '0' full, '`' reverse (11dc:1795)
void StarTrekEngine::setEnterpriseSpeed(int step) {
	if (_enterpriseState.inOrbit)
		return;
	ShipState &s = _enterpriseR3.ship;
	if (step < 0)
		s.requestedSpeed = (-s.maxSpeed) >> 3;
	else
		s.requestedSpeed = (int32)(((int64)step * s.maxSpeed) / 9);
}

// The speed bars on the top console (11dc:4213 draws them at 0x8f..0xa8,
// rows 0x14 and 0x16), a little more of them to touch; only on the bridge
bool StarTrekEngine::onSpeedBars(Common::Point p) {
	return !_spaceFullScreen && _bridgeDisplaysShown && p.x >= 0x8a && p.x <= 0xae && p.y >= 0x12 && p.y <= 0x1c;
}

// Along them, stop to full in the keys' 10 steps
void StarTrekEngine::setSpeedFromBars(Common::Point p) {
	int step = CLIP((p.x - 0x8f) * 10 / 0x19, 0, 9);
	setEnterpriseSpeed(step);
}

// Whether a point is in the space view, where the captain flies and fires
bool StarTrekEngine::inSpaceView(Common::Point p) {
	return _spaceFullScreen || _starfieldRect.contains(p);
}

// Fire, along the line from the eye through the cursor (11dc:1485)
void StarTrekEngine::fireEnterpriseWeapons(bool torpedoes, bool phasers) {
	if (_enterpriseState.inOrbit || _enterpriseR3.ship.destroyed)
		return;
	ShipState &s = _enterpriseR3.ship;
	bool ready = false;
	for (int i = 0; i < 4; i++) {
		if (s.weapons[i].nextFire < _frameIndex)
			ready = true;
	}
	if (!ready || s.weaponsArmed == 0)
		return;
	Common::Point mouse = _gfx->getMousePos();
	int32 v[3] = { (int16)(mouse.x - _starfieldXVar1), (int16)(_starfieldYVar1 - mouse.y), (int16)_starfieldPointDivisor };
	Point3 dir = fromShipAxes(v, _enterpriseR3.matrix);
	if (torpedoes) {
		bool fired = fireWeapon(&_enterpriseR3, 0, &dir);
		fired |= fireWeapon(&_enterpriseR3, 1, &dir);
		if (fired)
			_sound->playSoundEffectIndex(kSfxPhotonTorpedoes);
	}
	if (phasers) {
		bool fired = fireWeapon(&_enterpriseR3, 2, &dir);
		fired |= fireWeapon(&_enterpriseR3, 3, &dir);
		if (fired)
			_sound->playSoundEffectIndex(kSfxPhaser2);
	}
}

// Stopped, not turning (346c:091c)
void StarTrekEngine::stopShip(R3 *r3) {
	ShipState &s = r3->ship;
	s.requestedSpeed = 0;
	s.speed = 0;
	for (int i = 0; i < 3; i++) {
		s.targetTurnRate[i] = 0;
		s.turnRate[i] = 0;
	}
}

// Scotty, repair (2f0d:192c): what to repair first
void StarTrekEngine::setRepairTarget(int16 target) {
	_repairTarget = target;
}

// After a battle and a mission: everything mended, shields and weapons
// down, no extra power (2f0d:3129)
void StarTrekEngine::resetEnterpriseSystems() {
	_extraPowerUsed = false;
	ShipState &s = _enterpriseR3.ship;
	s.shieldsRequested = 0;
	s.weaponsRequested = 0;
	_enterpriseState.shields = false;
	_enterpriseState.weapons = false;
	for (int i = 0; i < 6; i++)
		s.shields[i].cur = s.shields[i].max;
	for (int i = 0; i < kNumShipSystems; i++)
		s.systems[i].cur = s.systems[i].max;
	_repairTarget = -1;
}

// ---- The bridge's displays (11dc:0c77, 0f0c, 3ec7, 13e4) ----

// The background's areas the displays draw in, pushed to the screen when
// they change (11dc:08e6), inclusive
static const int16 kDisplayRects[5][4] = {
	{ 90, 15, 126, 21 },   // phasers' bars
	{ 194, 15, 230, 21 },  // photon torpedoes' bars
	{ 140, 13, 178, 22 },  // power and speed
	{ 0, 66, 61, 107 },    // the ship from above
	{ 257, 68, 319, 108 }  // the ship from the side
};

void StarTrekEngine::initBridgeDisplays() {
	removeBridgeDisplays();
	for (int i = 0; i < 5; i++) {
		Sprite &sp = _displaySprites[i];
		Common::Rect r(kDisplayRects[i][0], kDisplayRects[i][1], kDisplayRects[i][2] + 1, kDisplayRects[i][3] + 1);
		initStarfieldSprite(&sp, new StubBitmap(0, 0), r);
	}
	// the scanner (17x17, at 0x9f,0x8c)
	Bitmap *scanner = new Bitmap(17, 17);
	scanner->xoffset = 8;
	scanner->yoffset = 8;
	_gfx->addSprite(&_scannerSprite);
	_scannerSprite.setBitmap(scanner);
	_scannerSprite.setXYAndPriority(0x9f, 0x8c, 2);
	// the running light (7x2, from 0x49,0x6b)
	Bitmap *light = new Bitmap(7, 2);
	memset(light->pixels, 0xda, 14);
	_gfx->addSprite(&_runningLightSprite);
	_runningLightSprite.setBitmap(light);
	_runningLightSprite.setXYAndPriority(0x49 + _runningLight * 8, 0x6b, 2);
	_bridgeDisplaysShown = true;
	updateBridgeDisplays(true);
}

void StarTrekEngine::removeBridgeDisplays() {
	if (!_bridgeDisplaysShown)
		return;
	for (int i = 0; i < 5; i++)
		_gfx->delSprite(&_displaySprites[i]);
	_gfx->delSprite(&_scannerSprite);
	_gfx->delSprite(&_runningLightSprite);
	_bridgeDisplaysShown = false;
}

// A horizontal line in the background (11dc:42ee)
void StarTrekEngine::displayLine(int x, int y, int len, byte color) {
	if (len > 0)
		memset(_gfx->getBackgroundPixels() + y * SCREEN_WIDTH + x, color, len);
}

// A bitmap at the place it says, its 0s clear (1da2:042d)
void StarTrekEngine::displayBitmap(const Common::String &name, int recolor) {
	// (kept: they're drawn every 4 ticks)
	Bitmap *&cached = _combatShapes["bmp:" + name];
	if (!cached) {
		Common::MemoryReadStreamEndian *file = _resource->loadFile(name + ".BMP", 0, false);
		if (!file)
			return;
		cached = new Bitmap(file, true);
	}
	const Bitmap &bmp = *cached;
	int x0 = -bmp.xoffset, y0 = -bmp.yoffset;
	byte *bg = _gfx->getBackgroundPixels();
	for (int y = 0; y < bmp.height; y++) {
		for (int x = 0; x < bmp.width; x++) {
			byte b = bmp.pixels[y * bmp.width + x];
			int sx = x0 + x, sy = y0 + y;
			if (b && sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT)
				bg[sy * SCREEN_WIDTH + sx] = recolor >= 0 ? recolor : b;
		}
	}
}

// The scanner: the enemies around, seen from the Enterprise (11dc:0c77)
void StarTrekEngine::updateScanner() {
	Bitmap *b = _scannerSprite.bitmap;
	memset(b->pixels, 0, 17 * 17);
	b->pixels[8 * 17 + 8] = 0x8b;
	static const byte kColors[4] = { 46, 120, 100, 86 };
	for (int i = 0; i < NUM_ENEMY_SHIPS; i++) {
		const R3 &s = _enemyShips[i];
		if (s.field1e != 2 || s.field54 < 50)
			continue;
		if (s.ship.cloaked && s.ship.cloakFading >= 0x14)
			continue;
		int16 a = tableAtan2(s.field36.y, s.field36.x);
		int32 hx = (int16)s.field36.x, hy = (int16)s.field36.y;
		int16 h = (int16)sqrt((double)(hx * hx + hy * hy));
		int16 e = tableAtan2(s.field36.z, h);
		int16 r = fx14Mul(0x10, trekSin((0x100 - e) >> 1));
		int px = (fx14Mul(r, trekCos(a)) + 1) / 2 + 8;
		int py = (fx14Mul(r, -trekSin(a)) + 1) / 2 + 8;
		if (px >= 0 && px < 17 && py >= 0 && py < 17)
			b->pixels[py * 17 + px] = kColors[i % 4];
	}
	_scannerSprite.bitmapChanged = true;
}

// The weapons' reload bars and lamps (11dc:3fb1)
void StarTrekEngine::updateWeaponBar(int weapon, int x, int y, bool odd) {
	static const int16 kLamp[4][3] = { { 0, 0, 0 } };
	(void)kLamp;
	const ShipState &s = _enterpriseR3.ship;
	const ShipSystem &mount = s.systems[kSystemWeapon0 + weapon];
	const ShipWeapon &w = s.weapons[weapon];
	int len = 24, srcX;
	byte color;
	if (!_enterpriseState.weapons) {
		color = 0x81;
		srcX = 0;
	} else if (!s.weaponsArmed || mount.cur < (mount.max >> 1)) {
		color = 0x81;
		srcX = 0x18;
	} else {
		color = 0xb1 + (odd ? 1 : 0);
		if (w.nextFire <= _frameIndex) {
			srcX = 0x10;
		} else {
			len = w.interval == 0 ? 24 : 24 - (int)(((w.nextFire - _frameIndex) * 24) / (uint32)w.interval);
			srcX = 8;
		}
	}
	len = CLIP(len, 0, 24);
	displayLine(x, y, len, color);
	displayLine(x + len, y, 24 - len, 0x81);
	// the lamp: a cell of PHASIND
	if (_phasindBitmap) {
		int srcY = x > 0xa0 ? 3 : 0;
		byte *bg = _gfx->getBackgroundPixels();
		for (int yy = 0; yy < 3; yy++)
			for (int xx = 0; xx < 8; xx++)
				bg[(y - 1 + yy) * SCREEN_WIDTH + x + 0x1b + xx] = _phasindBitmap->pixels[(srcY + yy) * _phasindBitmap->width + srcX + xx];
	}
}

void StarTrekEngine::updateBridgeDisplays(bool force) {
	if (!_bridgeDisplaysShown)
		return;
	const ShipState &e = _enterpriseR3.ship;

	updateScanner();

	// (11dc:3ec7) every tick: the weapons, the power, the speed
	if (!_phasindBitmap)
		_phasindBitmap = new Bitmap(_resource->loadBitmapFile("phasind"));
	static const int16 kWeaponBars[4][2] = { { 196, 16 }, { 194, 20 }, { 90, 16 }, { 92, 20 } };
	for (int i = 0; i < 4; i++)
		updateWeaponBar(i, kWeaponBars[i][0], kWeaponBars[i][1], i & 1);
	static const int16 kPowerBars[2][2] = { { 140, 13 }, { 140, 16 } };
	bool boosted = _extraPowerTicks != 0;
	for (int i = 0; i < 2; i++) {
		const ShipSystem &sys = e.systems[8 + i];
		int x = kPowerBars[i][0], y = kPowerBars[i][1];
		int v = sys.cur;
		if (boosted)
			v += _extraPower / 2;
		v = MIN<int>(v, sys.max);
		int n = sys.max == 0 ? 0 : v * 0x22 / sys.max;
		for (int k = 0; k < 2; k++) {
			displayLine(x, y + k, n, 0xb1);
			displayLine(x + n, y + k, 0x22 - n, 0x81);
			displayLine(x + 0x23, y + k, 4, boosted ? 0x69 : 0x81);
		}
	}
	// (11dc:4213) the speed wanted, and the speed
	if (e.maxSpeed != 0) {
		int n = MAX(0, (int)(((int64)e.speedTarget * 0x19) / e.maxSpeed));
		n = MIN(n, 0x19);
		displayLine(0x8f, 0x14, n, 0x69);
		displayLine(0x8f + n, 0x14, 0x19 - n, 0);
		n = MAX(0, (int)(((int64)e.speed * 0x18) / e.maxSpeed));
		n = MIN(n, 0x18);
		displayLine(0x90, 0x16, n, 0xba);
		displayLine(0x90 + n, 0x16, 0x18 - n, 0);
	}
	for (int i = 0; i < 3; i++)
		_displaySprites[i].bitmapChanged = true;

	// (11dc:13e4) the running light, every 4th tick
	if ((_frameIndex & 3) == 0) {
		_runningLight = (_runningLight + 1) % 0x16;
		_runningLightSprite.setXYAndPriority(_runningLight * 8 + 0x49, 0x6b, 2);
	}

	// (11dc:0f0c) the ship, or target analysis' ship, every 4th tick
	if ((_frameIndex & 3) != 0 && !force)
		return;
	const R3 *ship = &_enterpriseR3;
	if (_enterpriseState.targetAnalysis && _targetAnalysisShip) {
		if (_targetAnalysisShip->field1e == 0)
			_targetAnalysisShip = nullptr;
		else
			ship = _targetAnalysisShip;
	}
	static const char *const kClasses[] = { "ent", "klg", "ori", "rom" };
	const char *cls = kClasses[CLIP<int>(ship->field20, 0, 3)];
	displayBitmap(Common::String::format("t%s", cls));
	displayBitmap(Common::String::format("s%s", cls));
	const ShipState &s = ship->ship;
	for (int i = 0; i < kNumShipSystems; i++) {
		if (s.systems[i].cur * 4 < s.systems[i].max * 3) {
			displayBitmap(Common::String::format("t%s%02d", cls, i));
			displayBitmap(Common::String::format("s%s%02d", cls, i));
		}
	}
	for (int j = 0; j < 6; j++) {
		const ShipSystem &sh = s.shields[j];
		int color = !s.shieldsUp ? 0xb7 : 0xb0 + ((sh.max - sh.cur) * 8) / (sh.max + 1);
		displayBitmap(Common::String::format("%sshd%02d", cls, j), color);
	}
	_displaySprites[3].bitmapChanged = true;
	_displaySprites[4].bitmapChanged = true;
}

// ---- Saved games ----

static void syncShipState(Common::Serializer &ser, ShipState &s) {
	for (int i = 0; i < 6; i++) {
		ser.syncAsSint16LE(s.shields[i].enabled);
		ser.syncAsSint16LE(s.shields[i].max);
		ser.syncAsSint16LE(s.shields[i].cur);
	}
	for (int i = 0; i < kNumShipSystems; i++) {
		ser.syncAsSint16LE(s.systems[i].enabled);
		ser.syncAsSint16LE(s.systems[i].max);
		ser.syncAsSint16LE(s.systems[i].cur);
	}
	ser.syncAsSint32LE(s.maxTurnRate);
	ser.syncAsSint32LE(s.turnAccel);
	ser.syncAsSint32LE(s.maxSpeed);
	ser.syncAsSint32LE(s.speedAccel);
	ser.syncAsSint32LE(s.requestedSpeed);
	for (int i = 0; i < 3; i++)
		ser.syncAsSint32LE(s.targetTurnRate[i]);
	ser.syncAsSint32LE(s.speedTarget);
	ser.syncAsSint32LE(s.speed);
	for (int i = 0; i < 3; i++)
		ser.syncAsSint32LE(s.turnRate[i]);
	for (int i = 0; i < 3; i++)
		ser.syncAsSint32LE(s.turned[i]);
	ser.syncAsSint16LE(s.timer);
	ser.syncAsSint16LE(s.power);
	ser.syncAsSint16LE(s.shieldsRequested);
	ser.syncAsSint16LE(s.shieldsUp);
	ser.syncAsSint16LE(s.weaponsRequested);
	ser.syncAsSint16LE(s.weaponsArmed);
	ser.syncAsSint32LE(s.distance);
	ser.syncAsSint32LE(s.closing);
	ser.syncAsSint16LE(s.maneuvering);
	ser.syncAsSint16LE(s.maneuver);
	ser.syncAsSint16LE(s.maneuverStep);
	for (int i = 0; i < 4; i++) {
		ShipWeapon &w = s.weapons[i];
		ser.syncAsUint32LE(w.nextFire);
		ser.syncAsSint16LE(w.life);
		ser.syncAsSint16LE(w.speed);
		ser.syncAsSint16LE(w.interval);
		for (int j = 0; j < 3; j++)
			ser.syncAsSint32LE(w.offset[j]);
		ser.syncAsSint16LE(w.kind);
	}
	ser.syncAsSint32LE(s.closeRange);
	ser.syncAsSint16LE(s.canCloak);
	ser.syncAsSint16LE(s.cloakRequested);
	ser.syncAsSint16LE(s.cloaked);
	ser.syncAsSint16LE(s.cloakFading);
	ser.syncAsSint16LE(s.destroyed);
	ser.syncAsSint16LE(s.explosionTimer);
	ser.syncAsSint16LE(s.leaving);
}

static void syncShip(Common::Serializer &ser, R3 &r3) {
	for (int i = 0; i < 3; i++)
		ser.syncAsSint32LE(r3.pos[i]);
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			int16 v = r3.matrix[i][j].raw();
			ser.syncAsSint16LE(v);
			r3.matrix[i][j] = Fixed14::fromRaw(v);
		}
	}
	ser.syncAsSint16LE(r3.field1e);
	ser.syncAsSint16LE(r3.field20);
	ser.syncAsSint16LE(r3.field22);
	ser.syncAsSint16LE(r3.field24);
	ser.syncAsSint16LE(r3.speed.x);
	ser.syncAsSint16LE(r3.speed.y);
	ser.syncAsSint16LE(r3.speed.z);
	syncShipState(ser, r3.ship);
}

// The missions' state on the bridge, the Enterprise and the enemies (the
// original saves its 3D objects too)
void StarTrekEngine::syncSpaceState(Common::Serializer &ser) {
	ser.syncAsSint16LE(_bridgeHandler);
	ser.syncAsUint32LE(_bridgeTicks);
	for (int i = 0; i < 6; i++)
		ser.syncAsSint16LE(_missionFlags[i]);
	ser.syncAsSint16LE(_encounterCountdown);
	ser.syncAsByte(_bridgeInputDisabled);
	ser.syncAsSint16LE(_awayMissionScore);
	ser.syncAsSint16LE(_awayMissionMaxScore);
	int16 endFlag = _missionEndFlag;
	ser.syncAsSint16LE(endFlag);
	_missionEndFlag = endFlag;
	ser.syncString(_missionName);
	ser.syncAsSint16LE(_repairTarget);
	ser.syncAsByte(_extraPowerUsed);
	ser.syncAsSint32LE(_extraPowerTicks);
	ser.syncAsSint16LE(_extraPower);
	ser.syncAsSint16LE(_extraPowerBonus);
	ser.syncAsSint16LE(_extraPowerDuration);
	ser.syncAsByte(_enterpriseState.shields);
	ser.syncAsByte(_enterpriseState.weapons);
	ser.syncAsByte(_enterpriseState.targetAnalysis);

	if (ser.isLoading()) {
		removeShots();
		for (int i = 0; i < NUM_ENEMY_SHIPS; i++) {
			for (int j = 0; j < NUM_SPACE_OBJECTS; j++) {
				if (_r3List[j] == &_enemyShips[i])
					delR3(&_enemyShips[i]);
			}
			_enemyShips[i] = R3();
		}
	}
	Point3 pos = _enterpriseR3.pos;
	Matrix matrix = _enterpriseR3.matrix;
	syncShip(ser, _enterpriseR3);
	for (int i = 0; i < NUM_ENEMY_SHIPS; i++)
		syncShip(ser, _enemyShips[i]);
	if (ser.isLoading()) {
		_enterpriseR3.shipViews = true;
		_enterpriseR3.view = -1;
		(void)pos;
		(void)matrix;
		for (int i = 0; i < NUM_ENEMY_SHIPS; i++) {
			if (_enemyShips[i].field1e == 2) {
				_enemyShips[i].shipViews = true;
				addR3(&_enemyShips[i]);
			}
		}
		_inCombat = false;
		for (int i = 0; i < NUM_ENEMY_SHIPS; i++)
			_inCombat |= _enemyShips[i].field1e != 0;
		clearBridgeActions();
	}
}

// The space part of each tick on the bridge (11dc:1c82)
void StarTrekEngine::spaceCombatTick() {
	ShipState &e = _enterpriseR3.ship;
	e.shieldsRequested = _enterpriseState.shields;
	e.weaponsRequested = _enterpriseState.weapons;
	// (Insert / Delete: roll, every tick)
	if (!_enterpriseState.inOrbit)
		e.targetTurnRate[2] = _rollKey * e.maxTurnRate;
	bool wasInCombat = _inCombat;
	shipPower(&_enterpriseR3);
	moveShip(&_enterpriseR3);
	repairShip(&_enterpriseR3);
	enemyShipsTick();
	moveShots();
	// (orbit and navigation refuse while there are enemies)
	if (_inCombat)
		_enterpriseState.underAttack = true;
	else if (wasInCombat)
		_enterpriseState.underAttack = false;
	if ((_frameIndex & 0x3f) == 0) {
		for (int i = 0; i < NUM_SPACE_OBJECTS; i++) {
			R3 *r3 = _r3List[i];
			if (r3 && (r3->field1e == 1 || r3->field1e == 2))
				orthonormalize(r3->matrix);
		}
	}
}

} // End of namespace StarTrek
