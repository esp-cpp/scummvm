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

#include "startrek/resource.h"
#include "startrek/startrek.h"

namespace StarTrek {

void StarTrekEngine::initStarfieldPosition() {
	_starfieldPosition = Point3(0, 0, 0);
	_someMatrix = initMatrix();
}

// (1972:0028) The starfield in (x1, y1) - (x2, y2), inclusive, with a field
// of view of fov (1/1024ths of a turn, each side of the middle)
void StarTrekEngine::initStarfield(int16 x1, int16 y1, int16 x2, int16 y2, int16 fov) {
	_starfieldXVar1 = (x1 + x2) / 2;
	_starfieldYVar1 = (y1 + y2) / 2;
	_starfieldXVar2 = (x2 - x1 + 1) / 2;
	_starfieldYVar2 = (y2 - y1 + 1) / 2;

	_starfieldRect.left = _starfieldXVar1 - _starfieldXVar2;
	_starfieldRect.right = _starfieldXVar1 + _starfieldXVar2 + 1;
	_starfieldRect.top = _starfieldYVar1 - _starfieldYVar2;
	_starfieldRect.bottom = _starfieldYVar1 + _starfieldYVar2 + 1;

	for (uint i = 0; i < ARRAYSIZE(_starList); i++) {
		_starList[i].active = false;
		_starList[i].pos = Point3(0, 0, 0);
	}
	// (half the width over the tangent of the field of view)
	int16 tangent = (int16)round(tan(CLIP<int>(fov, 1, 0x80) * (2 * M_PI / 1024)) * 16384);
	_starfieldPointDivisor = ((int32)_starfieldXVar2 << 14) / tangent;
	_flt_50898 = 50.0;
}

void StarTrekEngine::addR3(R3 *r3) {
	for (int i = 0; i < NUM_SPACE_OBJECTS; i++) {
		if (_r3List[i] == nullptr) {
			_r3List[i] = r3;
			return;
		}
	}

	error("addR3: out of shapes.");
}

void StarTrekEngine::delR3(R3 *r3) {
	delete r3->bitmap;
	r3->bitmap = nullptr;
	r3->view = -1;

	for (int i = 0; i < NUM_SPACE_OBJECTS; i++) {
		if (_r3List[i] == r3) {
			_r3List[i] = nullptr;
			r3->field1e = 0;
			return;
		}
	}

	error("delR3: shape not found.");
}

void StarTrekEngine::clearStarfieldPixels() {
	_gfx->fillBackgroundRect(_starfieldRect, 0);
}

void StarTrekEngine::drawStarfield() {
	int16 var28 = ((_starfieldXVar2 * 3) >> 1);
	int16 xvar = var28 / 2;
	int16 var2a = ((_starfieldYVar2 * 3) >> 1);
	int16 yvar = var2a / 2;
	int16 var8 = _starfieldPointDivisor << 3;

	if (!_starsFile)
		_starsFile = _resource->loadFile("stars.shp");
	Common::MemoryReadStreamEndian *file = _starsFile;
	file->seek(0, SEEK_SET);

	for (int i = 0; i < NUM_STARS; i++) {
		Star *star = &_starList[i];
		if ((i & 0xf) == 0) {
			file->seek(0, SEEK_SET);
		}

		if (!star->active) {
			int16 var4 = getRandomWord() / var28 - xvar;
			int16 var6 = getRandomWord() / var2a - yvar;
			Point3 point = constructPoint3ForStarfield(var4, var6, var8);
			star->pos = matrixMult(_starPositionMatrix, point) + _starfieldPosition;
			star->active = true;
		}

		Point3 p = star->pos - _starfieldPosition;
		Point3 point2 = matrixMult(p, _starPositionMatrix);

		if (point2.z > _flt_50898 && point2.z < 0x3fff
		        && abs(point2.x) < point2.z && abs(point2.y) < point2.z) {

			int16 x = _starfieldXVar1 + scaleSpacePosition(point2.x, point2.z);
			int16 y = _starfieldYVar1 - scaleSpacePosition(point2.y, point2.z);

			int fileOffset = file->pos();
			file->readUint32();
			int16 width = file->readUint16();
			int16 height = file->readUint16();

			Common::Rect starRect(x, y, x + width, y + height);
			Common::Rect drawRect = _starfieldRect.findIntersectingRect(starRect);

			file->seek(fileOffset, SEEK_SET);

			Bitmap *bitmap = new Bitmap(file, false);
			if (!drawRect.isEmpty())
				_gfx->drawBitmapToBackground(starRect, drawRect, bitmap);
			delete bitmap;
		} else {
			star->active = false;

			file->readUint32();
			int16 offset2 = file->readUint16() * file->readUint16();
			file->seek(file->pos() + offset2, SEEK_SET);
		}
	}

}

/**
 * Compare 2 r3 objects for the purpose of sorting them by layer before drawing.
 * FIXME: Original returned an int, not a bool. This may affect the stability of the sort...
 */
bool compareR3Objects(R3 *obj1, R3 *obj2) {
	// the farthest first: the nearer ones are drawn over them
	return obj1->field54 > obj2->field54;
}

void StarTrekEngine::updateStarfieldAndShips(bool arg0) {
	bool enterpriseDestroyed = false;

	_starfieldSprite.bitmapChanged = true;
	_starPositionMatrix = _someMatrix.invert();
	clearStarfieldPixels();
	drawStarfield();

	int numObjects = 0;
	for (int i = 0; i < NUM_SPACE_OBJECTS; i++) {
		R3 *r3 = _r3List[i];
		if (r3 == nullptr)
			continue;
		r3->field34 = 0;

		switch (r3->field1e) {
		case 1:
		case 3:
			r3 = sub_19f24(r3);
			break;
		case 2:
			if (r3->ship.destroyed == 0) {
				r3 = sub_19f24(r3);
			} else if (++r3->ship.explosionTimer < 0x2e) {
				// exploding: 46 frames, bigger after 24
				int16 radius = r3->field22, size = r3->field24;
				bool big = r3->ship.explosionTimer >= 0x18;
				r3->field24 = big ? 12 : 6;
				r3->field22 = (big ? 2 : 1) * r3->field22;
				R3 *visible = sub_19f24(r3);
				r3->field22 = radius;
				r3->field24 = size;
				r3 = visible;
			} else {
				if (r3 == &_enterpriseR3)
					enterpriseDestroyed = true;
				else
					delR3(r3);
				r3 = nullptr;
			}
			break;
		default:
			break;
		}

		if (r3 != nullptr)
			_orderedR3List[numObjects++] = r3;
	}

	if (numObjects != 0) {
		Common::sort(_orderedR3List, _orderedR3List + numObjects, &compareR3Objects);

		for (int i = 0; i < numObjects; i++) {
			R3 *r3 = _orderedR3List[i];
			r3->field34 = 1;

			switch (r3->field1e) {
			case 1:
				drawR3Shape(r3);
				break;
			case 2:
				if (r3->ship.destroyed == 0) {
					drawR3Shape(r3);
				} else {
					// (1972:04dd) the ship for 21 frames, its explosion over it
					if (r3->ship.explosionTimer < 0x15)
						drawR3Shape(r3);
					drawShipExplosion(r3);
				}
				break;
			case 3:
				// a shot, or (in the intro) the planet and the credits
				if (r3 < _shots || r3 >= _shots + NUM_SHOTS)
					drawR3Shape(r3);
				else if (r3->field20 == 1)
					drawPhaserBeam(r3);
				else
					drawShot(r3);
				break;
			default:
				break;
			}
		}
	}

	if (enterpriseDestroyed) {
		showTextbox("", "GENE\\SPACE000#The Enterprise has been destroyed!", 20, 20, TEXTCOLOR_YELLOW, 0);
		showGameOverMenu();
		// FIXME: original game manipulates stack to jump somewhere...
		return;
	}

	// static, when the sensors are damaged (1972:019b)
	if (arg0)
		drawViewscreenStatic();
}

R3 *StarTrekEngine::sub_19f24(R3 *r3) {
	r3->matrix2 = r3->matrix;
	r3->field36 = r3->pos;
	r3->field36 -= _starfieldPosition;
	r3->field54 = r3->field36.getDiagonal();
	Point3 point = matrixMult(r3->field36, _starPositionMatrix);
	r3->field36 = point;

	if (sub_1c022(r3)) {
		if (r3->field1e >= 1 && r3->field1e <= 3)
			r3->matrix2 *= _starPositionMatrix;
		r3->field58 = _starfieldXVar1 + scaleSpacePosition(r3->field36.x, r3->field36.z);
		r3->field5a = _starfieldYVar1 - scaleSpacePosition(r3->field36.y, r3->field36.z);
		return r3;
	} else
		return nullptr;
}

// The original's trigonometry: angles in 1/1024ths of a turn, values 2.14
// fixed point.
int16 trekSin(int angle) {
	return (int16)round(sin((angle & 0x3ff) * (2 * M_PI / 1024)) * 16384);
}

int16 trekCos(int angle) {
	return trekSin(angle + 0x100);
}

// atan2(y, x) in (-0x200, 0x200]
int16 trekAtan2(int32 y, int32 x) {
	if (x == 0 && y == 0)
		return 0;
	return (int16)round(atan2((double)y, (double)x) * (512 / M_PI));
}

int16 trekAcos(int16 v) {
	return (int16)round(acos(CLIP<int>(v, -16384, 16384) / 16384.0) * (512 / M_PI));
}

int16 trekAsin(int16 v) {
	return (int16)round(asin(CLIP<int>(v, -16384, 16384) / 16384.0) * (512 / M_PI));
}

static int16 raw(const Matrix &m, int i, int j) {
	return m[i][j].raw();
}

static void setRaw(Matrix &m, int i, int j, int32 v) {
	m[i][j] = Fixed14::fromRaw((int16)v);
}

Matrix trekMatrixMultiply(const Matrix &a, const Matrix &b) {
	Matrix out;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			int32 sum = 0;
			for (int k = 0; k < 3; k++)
				sum += (int32)raw(a, i, k) * raw(b, k, j);
			setRaw(out, i, j, sum >> 14);
		}
	return out;
}

static Matrix rotationY(int16 angle) {
	Matrix r;
	int16 c = trekCos(angle), s = trekSin(angle);
	setRaw(r, 0, 0, c);
	setRaw(r, 0, 2, -s);
	setRaw(r, 1, 1, 0x4000);
	setRaw(r, 2, 0, s);
	setRaw(r, 2, 2, c);
	return r;
}

static Matrix rotationX(int16 angle) {
	Matrix r;
	int16 c = trekCos(angle), s = trekSin(angle);
	setRaw(r, 0, 0, 0x4000);
	setRaw(r, 1, 1, c);
	setRaw(r, 1, 2, s);
	setRaw(r, 2, 1, -s);
	setRaw(r, 2, 2, c);
	return r;
}

void trekPreRotateY(Matrix &m, int16 angle) {
	m = trekMatrixMultiply(rotationY(angle), m);
}

void trekPreRotateX(Matrix &m, int16 angle) {
	m = trekMatrixMultiply(rotationX(angle), m);
}

void trekPostRotateY(Matrix &m, int16 angle) {
	m = trekMatrixMultiply(m, rotationY(angle));
}

void trekPostRotateX(Matrix &m, int16 angle) {
	m = trekMatrixMultiply(m, rotationX(angle));
}

int32 trekLength(const Point3 &p) {
	double x = (int16)p.x, y = (int16)p.y, z = (int16)p.z;
	return (int32)round(sqrt(x * x + y * y + z * z));
}

// v / its length, 2.14 fixed point (the components' low 16 bits)
static bool normalize(const int32 v[3], int16 out[3]) {
	double x = (int16)v[0], y = (int16)v[1], z = (int16)v[2];
	int32 len = (int32)round(sqrt(x * x + y * y + z * z));
	for (int i = 0; i < 3; i++)
		out[i] = len ? (int16)CLIP<int32>((int32)(int16)v[i] * 16384 / len, -32768, 32767) : 0;
	return len != 0;
}

static void cross(const int16 a[3], const int16 b[3], int16 out[3]) {
	out[0] = (int16)(((int32)a[1] * b[2] - (int32)a[2] * b[1]) >> 14);
	out[1] = (int16)(((int32)a[2] * b[0] - (int32)a[0] * b[2]) >> 14);
	out[2] = (int16)(((int32)a[0] * b[1] - (int32)a[1] * b[0]) >> 14);
}

bool trekLookAt(const Point3 &dir, const Point_Fixed14 &up, Matrix &out) {
	int32 d[3] = { dir.x, dir.y, dir.z };
	int16 z[3], x[3], y[3];
	if (!normalize(d, z))
		return false;
	int16 u[3] = { up.x.raw(), up.y.raw(), up.z.raw() };
	cross(u, z, x);
	int32 xl[3] = { x[0], x[1], x[2] };
	normalize(xl, x);
	cross(z, x, y);
	for (int j = 0; j < 3; j++) {
		setRaw(out, 0, j, x[j]);
		setRaw(out, 1, j, y[j]);
		setRaw(out, 2, j, z[j]);
	}
	return true;
}

// (a * b + c * d) >> 14, as 16 bits
static int16 fx14MulAdd(int32 a, int16 b, int32 c, int16 d) {
	return (int16)((a * b + c * d) >> 14);
}

static int16 fx14Mul(int16 a, int16 b) {
	return (int16)(((int32)a * b) >> 14);
}

// a / b, 2.14 fixed point
static int16 fx14Div(int32 a, int32 b) {
	if (b == 0)
		return 0;
	return (int16)CLIP<int32>((int32)(int16)a * 16384 / (int16)b, -32768, 32767);
}

// The colour ramps planets' textures are coloured with (25 shades each)
static const byte kPlanetRamps[22][25] = {
	{ 115, 110, 105, 100, 162, 163, 164, 165, 152, 153, 154, 155, 156, 157, 158, 159, 203, 202, 201, 200, 199, 198, 197, 196, 195 },
	{ 99, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 200, 150, 151, 152, 153, 154, 155, 167, 157, 158, 175, 174, 173 },
	{ 140, 139, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 128, 129, 130, 29, 168, 169, 170, 171, 172, 173, 174 },
	{ 120, 216, 217, 218, 219, 176, 177, 178, 179, 180, 149, 150, 151, 152, 153, 154, 156, 157, 158, 159, 34, 169, 171, 172, 173 },
	{ 139, 137, 135, 134, 133, 132, 131, 7, 35, 10, 11, 40, 46, 95, 90, 176, 177, 85, 180, 56, 37, 34, 169, 171, 172 },
	{ 138, 136, 134, 74, 72, 73, 70, 69, 65, 63, 5, 36, 11, 40, 46, 95, 90, 66, 198, 37, 34, 169, 171, 172, 173 },
	{ 140, 139, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 129, 130, 131, 132, 133, 134, 135, 136, 136, 137, 138, 139 },
	{ 165, 152, 153, 154, 155, 156, 157, 158, 159, 191, 190, 189, 188, 187, 186, 185, 184, 169, 170, 171, 172, 173, 174, 175, 175 },
	{ 164, 163, 162, 163, 164, 165, 166, 167, 166, 165, 164, 163, 162, 163, 164, 163, 162, 163, 164, 165, 166, 167, 166, 165, 164 },
	{ 137, 138, 139, 140, 99, 49, 152, 153, 154, 155, 156, 191, 190, 189, 188, 187, 186, 185, 184, 170, 171, 172, 173, 174, 174 },
	{ 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150 },
	{ 175, 1, 1, 128, 128, 1, 1, 167, 1, 1, 128, 128, 1, 1, 175, 1, 1, 128, 128, 1, 1, 183, 1, 1, 128 },
	{ 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 206, 205, 204, 203, 202, 201, 200, 199, 198 },
	{ 172, 171, 170, 171, 172, 173, 174, 175, 174, 173, 172, 171, 170, 171, 172, 171, 170, 171, 172, 173, 174, 175, 174, 173, 172 },
	{ 180, 179, 178, 179, 180, 181, 182, 183, 182, 181, 180, 179, 178, 179, 180, 179, 178, 179, 180, 181, 182, 183, 182, 181, 180 },
	{ 121, 120, 216, 217, 218, 219, 176, 219, 218, 217, 216, 120, 121, 120, 216, 217, 218, 219, 176, 219, 218, 217, 216, 120, 121 },
	{ 140, 140, 140, 140, 99, 99, 99, 49, 49, 99, 99, 99, 140, 140, 140, 140, 99, 99, 99, 49, 49, 99, 99, 99, 140 },
	{ 110, 105, 105, 161, 161, 161, 162, 161, 161, 161, 105, 105, 110, 105, 105, 161, 161, 161, 162, 161, 161, 161, 105, 105, 110 },
	{ 188, 187, 186, 187, 188, 189, 190, 191, 190, 189, 188, 187, 186, 187, 188, 187, 186, 187, 188, 189, 190, 191, 190, 189, 188 },
	{ 208, 209, 210, 211, 212, 213, 214, 215, 208, 209, 210, 211, 212, 213, 214, 215, 208, 209, 210, 211, 212, 213, 214, 215, 208 },
	{ 224, 225, 226, 227, 228, 229, 230, 231, 224, 225, 226, 227, 228, 229, 230, 231, 224, 225, 226, 227, 228, 229, 230, 231, 224 },
	{ 232, 233, 234, 235, 236, 237, 238, 239, 232, 233, 234, 235, 236, 237, 238, 239, 232, 233, 234, 235, 236, 237, 238, 239, 232 }
};

// The original's rand() (Microsoft C)
struct TrekRandom {
	uint32 seed;
	explicit TrekRandom(uint32 s) : seed(s) {}
	int16 next() {
		seed = seed * 214013 + 2531011;
		return (seed >> 16) & 0x7fff;
	}
};

// A planet's texture (2d4b:0e6e): a fractal height map, midpoint
// displacement wrapping round, coloured with one of the ramps
static byte *makePlanetTexture(int size, uint16 seed, uint16 color) {
	TrekRandom rnd(seed);
	color = MIN<uint16>(color, 21);
	Common::Array<int16> h;
	h.resize(size * size);
	for (int i = 0; i < size * size; i++)
		h[i] = 0;

	struct Fractal {
		Common::Array<int16> &h;
		int size;
		TrekRandom &rnd;
		void line(int x1, int y1, int x2, int y2) {
			int dx = (x2 - x1) >> 1, dy = (y2 - y1) >> 1;
			int16 a = h[x1 * size + y1];
			int16 b = h[((size - 1) & x2) * size + ((size - 1) & y2)];
			int16 r = rnd.next();
			h[(x1 + dx) * size + y1 + dy] = (int16)((int16)(a + b) / 2 + (int16)(r - 0x4000) % (dx + dy));
			if (dx + dy > 1) {
				line(x1, y1, x1 + dx, y1 + dy);
				line(x1 + dx, y1 + dy, x2, y2);
			}
		}
		void square(int x1, int y1, int x2, int y2) {
			int hx = (x2 - x1) >> 1, hy = (y2 - y1) >> 1;
			line(x1, y1 + hy, x2, y1 + hy);
			if (hx + hy > 2) {
				line(x1 + hx, y1, x1 + hx, y1 + hy);
				line(x1 + hx, y1 + hy, x1 + hx, y2);
			}
			if (hx + hy > 3) {
				square(x1, y1, x1 + hx, y1 + hy);
				square(x1 + hx, y1, x2, y1 + hy);
				square(x1, y1 + hy, x1 + hx, y2);
				square(x1 + hx, y1 + hy, x2, y2);
			}
		}
	} fractal = { h, size, rnd };
	fractal.line(0, 0, size, 0);
	fractal.line(0, 0, 0, size);
	fractal.square(0, 0, size, size);

	int16 hi = -0x8000, lo = 0x7fff;
	for (int i = 0; i < size * size; i++) {
		hi = MAX(hi, h[i]);
		lo = MIN(lo, h[i]);
	}
	byte *texture = new byte[size * size];
	for (int i = 0; i < size * size; i++)
		texture[i] = kPlanetRamps[color][((int32)(h[i] - lo) * 25) / ((int32)(hi - lo) + 1)];
	return texture;
}

void StarTrekEngine::initPlanetR3(R3 *r3, int16 size, int16 small, uint16 seed, uint16 color) {
	freePlanetR3(r3);
	r3->field1e = 1;
	r3->planet = true;
	r3->planetSize = size;
	r3->planetSmall = small;
	r3->planetSeed = seed;
	r3->planetColor = color;
	r3->field22 = size * 11;
}

void StarTrekEngine::freePlanetR3(R3 *r3) {
	delete[] r3->planetTexture;
	r3->planetTexture = nullptr;
	delete r3->bitmap;
	r3->bitmap = nullptr;
}

// The planet as it's seen now (2d4b:0008): the texture, turned to face the
// camera the way the planet does, mapped onto a disc whose size steps with
// the distance
bool StarTrekEngine::selectPlanetView(R3 *r3) {
	if (!_planetTables) {
		Common::MemoryReadStreamEndian *file = _resource->loadFile("PLANET.DAT", 0, false);
		if (!file)
			return false;
		_planetTables = new byte[0x10000]; // (read with 16-bit offsets)
		memset(_planetTables, 0, 0x10000);
		file->read(_planetTables, MIN<int64>(file->size(), 0x761a));
		delete file;
	}
	const int textureSize = r3->planetSmall ? 0x20 : 0x80;
	if (!r3->planetTexture)
		r3->planetTexture = makePlanetTexture(textureSize, r3->planetSeed, r3->planetColor);

	// the disc: the closer, the bigger the image (and the smaller the scale)
	int32 far = (int32)_starfieldPointDivisor * r3->planetSize;
	int32 z = r3->field36.z;
	uint16 table;
	int n;
	if (z >= far) {
		table = 0x4000;
		r3->field24 = r3->planetSize;
		n = 11;
	} else if (z >= far / 2) {
		table = 0x40f2;
		r3->field24 = r3->planetSize / 2;
		n = 21;
	} else if (r3->planetSmall || z >= far / 3) {
		table = 0x4464;
		r3->field24 = r3->planetSize / 3;
		n = 31;
	} else if (z >= far / 4) {
		table = 0x4be6;
		r3->field24 = r3->planetSize / 4;
		n = 41;
	} else {
		table = 0x5908;
		r3->field24 = r3->planetSize / 6;
		n = 61;
	}

	// which side of it the camera sees
	Point3 toCamera = Point3(0, 0, 0) - r3->field36;
	Point3 p = matrixMult(toCamera, r3->matrix2.invert());
	int16 heading = trekAtan2(p.x, p.z) - 0x200;
	int16 pitch = trekAsin(fx14Div(p.y, r3->field54));
	if (pitch < -0x80 || pitch > 0x80) {
		int32 h = (int32)sqrt((double)(int16)p.x * (int16)p.x + (double)(int16)p.z * (int16)p.z);
		pitch = trekAcos(fx14Div(h, r3->field54));
		if (p.y < 0)
			pitch = -pitch;
	}
	Matrix view = initMatrix();
	trekPostRotateY(view, -heading);
	trekPostRotateX(view, pitch);
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r3->viewMatrix[i][j] = view[i][j].raw();
	r3->viewMirrored = false;

	uint16 lat = ((((int32)pitch << 7) + 0x200) / 0x400 & 0x7f) << 7;
	uint16 lon = ((((int32)-heading << 7) + 0x200) / 0x400 & 0x7f) << 7;

	delete r3->bitmap;
	r3->bitmap = new Bitmap(n, n);
	r3->bitmap->xoffset = (n - 1) / 2;
	r3->bitmap->yoffset = (n - 1) / 2;
	byte *out = r3->bitmap->pixels;
	const byte *tables = _planetTables;
	auto word = [tables](uint32 offset) {
		return (uint16)(tables[offset & 0xffff] | (tables[(offset + 1) & 0xffff] << 8));
	};
	uint32 si = table;
	int count = n * n;
	while (count > 0) {
		uint16 bx = word(si) + lat;
		si += 2;
		if (bx & 0x8000) {
			// a run outside the disc
			while (true) {
				*out++ = 0;
				count--;
				if (count == 0 || !(word(si) & 0x8000))
					break;
				si += 2;
			}
			continue;
		}
		bx = (word(bx & 0xbfff) + lon) & 0xbfff;
		if (r3->planetSmall) {
			uint16 v = bx << 1;
			v = (((v >> 8) >> 2) << 8) | (v & 0xff);
			bx = v >> 3;
		}
		*out++ = bx < textureSize * textureSize ? r3->planetTexture[bx] : 0;
		count--;
	}
	return true;
}

static const char *const kShipNames[] = { "ENT", "KLING", "ORION", "ROM" };

void StarTrekEngine::initEnterpriseR3() {
	_enterpriseR3 = R3();
	_enterpriseR3.matrix = initMatrix();
	_enterpriseR3.field1e = 2;
	_enterpriseR3.shipViews = true;
	// its class (the views to draw it with), radius, size, systems, flight
	// model and weapons
	loadShipClass(&_enterpriseR3, "ENTERPRI");
	addR3(&_enterpriseR3);
}

void StarTrekEngine::releaseShipView(R3 *r3) {
	delete r3->bitmap;
	r3->bitmap = nullptr;
	r3->view = -1;
}

bool StarTrekEngine::selectShipView(R3 *r3) {
	if (r3->field20 < 0 || r3->field20 >= ARRAYSIZE(kShipNames))
		return false;

	// Where the camera is, seen from the ship
	Point3 toCamera = Point3(0, 0, 0) - r3->field36;
	Point3 p = matrixMult(toCamera, r3->matrix2.invert());

	int32 heading = trekAtan2(p.x, p.z);
	if (heading < 0)
		heading += 0x400;

	// from above (0) to below (0x200)
	int32 pitch = trekAcos(fx14Div(p.y, r3->field54));
	if (pitch < 0x80 || pitch > 0x180) {
		int32 h = (int32)sqrt((double)(int16)p.x * (int16)p.x + (double)(int16)p.z * (int16)p.z);
		pitch = trekAsin(fx14Div(h, r3->field54));
		if (p.y < 0)
			pitch = 0x200 - pitch;
	}
	int pitchIndex = MIN<int32>(6, (pitch * 6 + 0x100) / 0x200);

	heading += 0x2a;
	if (heading >= 0x400)
		heading -= 0x400;
	int headingIndex = heading * 12 / 0x400;

	// The views: 7 headings (from behind round to the front) at 5 pitches,
	// plus straight above and below; the other 5 headings are mirrored
	int16 view;
	if (pitchIndex == 0)
		view = 0x00;
	else if (pitchIndex == 6)
		view = 0x0c;
	else if (headingIndex <= 6)
		view = (headingIndex << 5) | (pitchIndex << 1);
	else
		view = 0x8000 | ((12 - headingIndex) << 5) | (pitchIndex << 1);

	if (view == r3->view && r3->bitmap)
		return true;
	releaseShipView(r3);

	Common::String name = Common::String::format("%s%02x.R3S", kShipNames[r3->field20], (view >> 1) & 0x77);
	Common::MemoryReadStreamEndian *file = _resource->loadFile(name, 0, false);
	if (!file)
		return false;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			r3->viewMatrix[i][j] = file->readSint16();
	r3->viewMirrored = (view & 0x8000) != 0;
	if (r3->viewMirrored) {
		r3->viewMatrix[0][1] = -r3->viewMatrix[0][1];
		r3->viewMatrix[0][2] = -r3->viewMatrix[0][2];
		r3->viewMatrix[1][0] = -r3->viewMatrix[1][0];
		r3->viewMatrix[2][0] = -r3->viewMatrix[2][0];
	}
	file->seek(0x24, SEEK_SET);
	r3->bitmap = new Bitmap(file, true);
	r3->view = view;
	return true;
}

void StarTrekEngine::drawR3Shape(R3 *r3) {
	// (nothing at the camera or behind it: the camera may be the Enterprise's)
	if (r3->field36.z <= 0)
		return;
	if (r3->shipViews && r3->field1e == 2 && !selectShipView(r3))
		return;
	if (r3->planet && !selectPlanetView(r3))
		return;

	if (r3->bitmap != nullptr && r3->field36.z > 0) {
		double dbl68 = ((double)r3->field24 * _starfieldPointDivisor) / r3->field36.z;
		double dbl70 = 1.0 / dbl68;

		double dbl30 = 1.0;
		double dbl28 = 0.0;
		double dbl20 = 0.0;
		double dbl18 = 1.0;
		double dbl60 = 1.0;
		double dbl58 = 0.0;
		double dbl50 = 0.0;
		double dbl48 = 0.0;

		// The orientation of the shape as it's stored, and as it's seen now
		const int16 (*v)[3] = r3->viewMatrix;
		int16 m[3][3];
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				m[i][j] = r3->matrix2[i][j].raw();

		switch (r3->field1e) {
		case 1:
		case 4: {
			// rotated in the screen's plane only
			int32 r00 = 0, r01 = 0;
			for (int k = 0; k < 3; k++) {
				r00 += (int32)v[k][0] * m[k][0];
				r01 += (int32)v[k][0] * m[k][1];
			}
			r00 = (int16)(r00 >> 14);
			r01 = (int16)(r01 >> 14);
			int16 angle = trekAtan2(r01, r00);
			double c = trekCos(angle) / 16384.0;
			double sn = trekSin(angle) / 16384.0;
			dbl30 = c;
			dbl28 = -sn;
			dbl20 = sn;
			dbl18 = c;
			dbl60 = dbl18;
			dbl58 = -dbl28;
			dbl50 = -dbl20;
			dbl48 = dbl30;
			dbl68 = CLIP(dbl68, 0.01, 10.0);
			dbl30 *= dbl68;
			dbl28 *= dbl68;
			dbl20 *= dbl68;
			dbl18 *= dbl68;
			dbl60 /= dbl68;
			dbl58 /= dbl68;
			dbl50 /= dbl68;
			dbl48 /= dbl68;
			break;
		}
		case 3:
			dbl30 = dbl68;
			dbl28 = 0.0;
			dbl20 = 0.0;
			dbl18 = dbl68;
			dbl60 = dbl70;
			dbl58 = 0.0;
			dbl50 = 0.0;
			dbl48 = dbl70;
			break;
		default: {
			// A ship: the bitmap's two axes are the two of the view's that
			// face the camera most; where those land on the screen now
			// (with the perspective at the ship's position) maps the bitmap.
			int a = ABS(fx14MulAdd(v[0][0], v[1][1], -v[1][0], v[0][1])) / 4;
			int b = ABS(fx14MulAdd(v[1][0], v[2][1], -v[2][0], v[1][1])) / 2;
			int c = ABS(fx14MulAdd(v[0][0], v[2][1], -v[2][0], v[0][1])) / 1;
			int iA = 2, iB = 0;
			if (a > b) {
				if (a > c)
					iA = 1;
			} else if (b > c) {
				iB = 1;
			}

			int16 e0 = fx14MulAdd(v[iA][1], m[iB][0], -v[iB][1], m[iA][0]);
			int16 e1 = fx14MulAdd(v[iA][1], m[iB][1], -v[iB][1], m[iA][1]);
			int16 e2 = fx14MulAdd(v[iA][1], m[iB][2], -v[iB][1], m[iA][2]);
			int16 e3 = fx14MulAdd(-v[iA][0], m[iB][0], v[iB][0], m[iA][0]);
			int16 e4 = fx14MulAdd(-v[iA][0], m[iB][1], v[iB][0], m[iA][1]);
			int16 e5 = fx14MulAdd(-v[iA][0], m[iB][2], v[iB][0], m[iA][2]);

			// (only near the line of sight: the perspective terms below
			// are 16 bits)
			int16 limit = (int16)(ABS(r3->field36.z) << 1);
			if (ABS(r3->field36.x) >= limit || ABS(r3->field36.y) >= limit)
				return;

			int16 xz = fx14Div(r3->field36.x, r3->field36.z);
			int16 yz = fx14Div(r3->field36.y, r3->field36.z);
			dbl30 = (int16)(e0 - fx14Mul(e2, xz)) / 16384.0;
			dbl28 = (int16)(e1 - fx14Mul(e2, yz)) / 16384.0;
			dbl20 = (int16)(e3 - fx14Mul(e5, xz)) / 16384.0;
			dbl18 = (int16)(e4 - fx14Mul(e5, yz)) / 16384.0;

			double det2 = fx14MulAdd(v[iB][0], v[iA][1], -v[iA][0], v[iB][1]) / 16384.0;
			dbl68 /= det2;
			dbl28 = -dbl28;
			dbl20 = -dbl20;
			if (r3->viewMirrored) {
				dbl30 = -dbl30;
				dbl28 = -dbl28;
			}
			double det = dbl30 * dbl18 - dbl28 * dbl20;
			dbl70 = (1.0 / dbl68) / det;
			dbl60 = dbl18;
			dbl58 = -dbl28;
			dbl50 = -dbl20;
			dbl48 = dbl30;
			dbl30 *= dbl68;
			dbl28 *= dbl68;
			dbl20 *= dbl68;
			dbl18 *= dbl68;
			dbl60 *= dbl70;
			dbl58 *= dbl70;
			dbl50 *= dbl70;
			dbl48 *= dbl70;
			break;
		}
		}

		r3->field80 = dbl60;
		r3->field88 = dbl58;
		r3->field90 = dbl50;
		r3->field98 = dbl48;

		// dbl30, (bitmap->xoffset + 2), r3->field58,
		double tmp = r3->field58 - ((double)r3->bitmap->xoffset + 2) * dbl30;
		// dbl20, (bitmap->yoffset + 2), tmp
		double dbl10 = tmp - ((double)r3->bitmap->yoffset + 2) * dbl20;

		// dbl28, (bitmap->xoffset + 2), r3->field5a
		tmp = r3->field5a - ((double)r3->bitmap->xoffset + 2) * dbl28;
		// dbl18, (bitmap->yoffset + 2), tmp
		double dbl8 = tmp - ((double)r3->bitmap->yoffset + 2) * dbl18;

		// dbl60, r3->field58, bitmap->xoffset + 2
		tmp = ((double)r3->bitmap->xoffset + 2) - dbl60 * r3->field58;
		double dbl40 = tmp - dbl50 * r3->field5a;

		tmp = ((double)r3->bitmap->yoffset + 2) - dbl58 * r3->field58;
		double dbl38 = tmp - dbl48 * r3->field5a;

		double dbl3e4 = (double)r3->bitmap->width + 2;
		double dbl3ec = (double)r3->bitmap->height + 2;

		double thing[8];
		tmp = 1.0 * dbl30;
		thing[0] = 1.0 * dbl20 + tmp + dbl10;

		tmp = 1.0 * dbl28;
		thing[1] = 1.0 * dbl18 + tmp + dbl8;

		tmp = 1.0 * dbl30;
		thing[2] = dbl3ec * dbl20 + tmp + dbl10;

		tmp = 1.0 * dbl28;
		thing[3] = dbl3ec * dbl18 + tmp + dbl8;

		thing[4] = dbl3ec * dbl20 + dbl3e4 * dbl30 + dbl10;
		thing[5] = dbl3ec * dbl18 + dbl3e4 * dbl28 + dbl8;

		thing[6] = 1.0 * dbl20 + dbl3e4 * dbl30 + dbl10;
		thing[7] = 1.0 * dbl18 + dbl3e4 * dbl28 + dbl8;

		int16 *rightBounds = new int16[SCREEN_HEIGHT];
		int16 *leftBounds = new int16[SCREEN_HEIGHT];

		for (int y = _starfieldRect.top; y < _starfieldRect.bottom; y++) {
			leftBounds[y] = _starfieldRect.right;
			rightBounds[y] = _starfieldRect.left - 1;
		}

		int16 shpImageTop = _starfieldRect.bottom - 1;
		int16 shpImageBottom = _starfieldRect.top;

		for (int i = 0; i < 4; i++) {
			int16 index1 = i;
			int16 index2 = (i + 1) & 3;

			if (thing[index1 * 2 + 1] > thing[index2 * 2 + 1]) {
				index1 = index2;
				index2 = i;
			}

			int16 top = ceil(thing[index1 * 2 + 1]);
			int16 bottom = floor(thing[index2 * 2 + 1]);

			if (top > bottom)
				continue;
			if (top > _starfieldRect.bottom - 1)
				continue;
			if (bottom < _starfieldRect.top)
				continue;
			if (top < _starfieldRect.top)
				top = _starfieldRect.top;
			if (bottom > _starfieldRect.bottom - 1)
				bottom = _starfieldRect.bottom - 1;

			if (top < shpImageTop)
				shpImageTop = top;
			if (bottom > shpImageBottom)
				shpImageBottom = bottom;

			double dbl3f4;
			if (thing[index2 * 2 + 1] == thing[index1 * 2 + 1])
				dbl3f4 = 0.0;
			else
				dbl3f4 = (thing[index2 * 2] - thing[index1 * 2]) / (thing[index2 * 2 + 1] - thing[index1 * 2 + 1]);

			int32 boundDiff = (int32)(0x10000 * dbl3f4); // var3ec
			int32 boundBase = (int32)(((top - thing[index1 * 2 + 1]) * dbl3f4 + thing[index1 * 2]) * 0x10000); // var3e8

			for (int y = top; y <= bottom; y++) {
				int16 rightBound = boundBase >> 16; // var3f6
				int16 leftBound = (boundBase + 0xffff) >> 16; // var3f8

				if (leftBound < _starfieldRect.left)
					leftBound = _starfieldRect.left;
				if (leftBound < leftBounds[y])
					leftBounds[y] = leftBound;

				if (rightBound > _starfieldRect.right - 1)
					rightBound = _starfieldRect.right - 1;
				if (rightBound > rightBounds[y])
					rightBounds[y] = rightBound;

				boundBase += boundDiff;
			}
		}

		while (shpImageTop <= shpImageBottom) {
			if (leftBounds[shpImageTop] > rightBounds[shpImageTop])
				shpImageTop++;
			else
				break;
		}
		while (shpImageTop <= shpImageBottom) {
			if (leftBounds[shpImageBottom] > rightBounds[shpImageBottom])
				shpImageBottom--;
			else
				break;
		}

		if (shpImageTop <= shpImageBottom) {
			// Far enough to draw the bitmap as it is; closer, it's magnified,
			// and the pixels are sampled with an ordered dither (seeded per
			// shape) so the magnification doesn't show blocks
			int32 nearDistance = ((int32)r3->field24 * _starfieldPointDivisor) << 1;
			bool var3fa;
			if (r3->field1e == 2)
				var3fa = !_byte_45b3c || nearDistance / 5 <= r3->field36.z;
			else
				var3fa = nearDistance / 3 <= r3->field36.z;

			uint16 ditherSeed = 0;
			if (!var3fa) {
				if (r3->field1e == 3) {
					ditherSeed = (uint16)(_frameIndex >> 3) ^ (uint16)_frameIndex;
				} else {
					uint16 sum = 0;
					for (int i = 0; i < 3; i++)
						for (int j = 0; j < 3; j++)
							sum += r3->viewMatrix[i][j];
					ditherSeed = (((int16)sum >> 3) + ((int16)sum >> 6)) ^ sum;
				}
			}

			// Amount added to X/Y positions after each pixel is drawn
			int16 xDiff = (int16)(dbl60 * 256);
			int16 yDiff = (int16)(dbl58 * 256);

			int16 var3f2 = (int16)(dbl50 * 256);
			int16 var3f4 = (int16)(dbl48 * 256);

			int16 var3f6 = var3f2 * shpImageTop + (int16)(dbl40 * 256);
			int16 var3f8 = var3f4 * shpImageTop + (int16)(dbl38 * 256);

			Bitmap tmpBitmap(256, 256); // (sampled with 16-bit offsets: a whole 64 KB "segment")

			int16 bitmapWidth = r3->bitmap->width;
			int16 bitmapHeight = r3->bitmap->height;

			if (bitmapHeight > 245)
				error("Shape height too big in drawR3Shape!");
			if (bitmapWidth > 252)
				error("Shape width too big in drawR3Shape!");

			// The shape goes in with a transparent border round it, which the
			// edges' samples land in. (Everything else is cleared too: the
			// buffer is fresh, and sampling wraps in its 64 KB.)
			memset(tmpBitmap.pixels, 0, 256 * 256);

			// (a cloaking ship fades through the cloak palettes, 1972:2453)
			const byte *lut = nullptr;
			if (r3->field1e == 2 && r3->ship.canCloak != 0) {
				ShipState &s = r3->ship;
				s.cloakFading = CLIP<int16>(s.cloakFading + (s.cloaked ? 1 : -1), 0, 0x1c);
				if (s.cloakFading >= 0x14)
					lut = cloakTable(s.cloakFading - 0x14);
			}
			_gfx->copyRectBetweenBitmaps(&tmpBitmap, 2, 2, r3->bitmap, 0, 0, bitmapWidth, bitmapHeight);
			if (lut) {
				for (int yy = 0; yy < bitmapHeight; yy++) {
					byte *row = tmpBitmap.pixels + (yy + 2) * 256 + 2;
					for (int xx = 0; xx < bitmapWidth; xx++)
						row[xx] = lut[row[xx]];
				}
			}

			byte *bgPixels = _gfx->getBackgroundPixels() + shpImageTop * SCREEN_WIDTH;

			for (int y = shpImageTop; y <= shpImageBottom; y++) {
				int16 leftBound = leftBounds[y];
				int16 rowWidth = rightBounds[y] - leftBound;

				int16 srcX = leftBound * xDiff + var3f6;
				int16 srcY = leftBound * yDiff + var3f8;
				var3f6 += var3f2;
				var3f8 += var3f4;

				byte *bgPixels2 = bgPixels + leftBound;
				bgPixels += SCREEN_WIDTH;

				if (rowWidth == 0)
					continue;

				// (the sub-pixel offsets of the dither, in the original's order)
				static const byte kDither[8] = { 0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0 };
				int phase = (y + leftBound + ditherSeed) & 7;

				int16 cx = srcX;
				int16 bx = srcY;
				byte *di = bgPixels2;

				for (int a = 0; a < rowWidth; a++) {
					int16 offset = var3fa ? 0x80 : kDither[(phase + a) & 7];
					byte b = tmpBitmap.pixels[((int16)(bx + offset) & 0xff00) + ((uint16)(int16)(cx + offset) >> 8)];
					cx += xDiff;
					bx += yDiff;
					if (b == 0)
						di++;
					else
						*(di++) = b;
				}
			}

		}

		delete[] rightBounds;
		delete[] leftBounds;
	}

	if (r3->funcPtr2 != 0) {
		// TODO: call it
	}
}

// Whether a shape (its radius, field22, around it) is in the view: the four
// planes through the camera and the starfield's edges
bool StarTrekEngine::sub_1c022(R3 *r3) {
	const int64 x = r3->field36.x, y = r3->field36.y, z = r3->field36.z;
	const int64 r = r3->field22;
	const int64 d = _starfieldPointDivisor, hw = _starfieldXVar2, hh = _starfieldYVar2;
	int64 horizontal = x <= 0 ? d * (x + r) + hw * z : hw * z - d * (x - r);
	int64 vertical = y <= 0 ? d * (y + r) + hh * z : hh * z - d * (y - r);
	return horizontal > 0 && vertical > 0;
}

Point3 StarTrekEngine::constructPoint3ForStarfield(int16 x, int16 y, int16 z) {
	Point3 point;
	point.z = z;
	point.y = y * z / _starfieldPointDivisor;
	point.x = x * z / _starfieldPointDivisor;

	return point;
}

Point3 StarTrekEngine::matrixMult(const Matrix &weight, const Point3 &point) {
	Point3 p;
	for (int i = 0; i < 3; i++) {
		p[i] = 0;
		for (int j = 0; j < 3; j++)
			p[i] += (int16)weight[i][j].multToInt(point[j]);
	}
	return p;
}

Point3 StarTrekEngine::matrixMult(const Point3 &point, const Matrix &weight) {
	Point3 p = Point3();
	for (int i = 0; i < 3; i++) {
		p[i] = 0;
		for (int j = 0; j < 3; j++)
			p[i] += (int16)weight[j][i].multToInt(point[j]);
	}
	return p;
}

int32 StarTrekEngine::scaleSpacePosition(int32 x, int32 z) {
	if (x == 0 || z == 0)
		return 0;
	return (x * _starfieldPointDivisor) / z;
}

Matrix StarTrekEngine::initMatrix() {
	Matrix mat;
	mat[0][0] = 1.0;
	mat[1][1] = 1.0;
	mat[2][2] = 1.0;
	return mat;
}

Matrix StarTrekEngine::initSpeedMatrixForXZMovement(Angle angle, const Matrix &matrix) {
	Fixed14 sinVal = sin(angle.toDouble());
	Fixed14 cosVal = cos(angle.toDouble());

	Matrix matrix1 = initMatrix();
	matrix1[0].x = cosVal;
	matrix1[0].z = -sinVal;
	matrix1[2].x = sinVal;
	matrix1[2].z = cosVal;

	return matrix * matrix1;
}

} // End of namespace StarTrek
