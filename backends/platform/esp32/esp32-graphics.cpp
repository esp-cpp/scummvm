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

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "backends/platform/esp32/esp32-graphics.h"

#include "common/system.h"
#include "common/textconsole.h"

#include <esp_heap_caps.h>

#include "box-emu.hpp"

static const Graphics::PixelFormat kRgb565(2, 5, 6, 5, 0, 11, 5, 0, 0);

static inline uint16_t rgb565(byte r, byte g, byte b) {
	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static void *psramAlloc(size_t bytes) {
	return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

Esp32GraphicsManager::Esp32GraphicsManager() {
	_overlay.init(320, 200, 320 * 2, psramAlloc(320 * 200 * 2), kRgb565);
}

Esp32GraphicsManager::~Esp32GraphicsManager() {
	heap_caps_free(_screen.getPixels());
	heap_caps_free(_overlay.getPixels());
	heap_caps_free(_out[0]);
	heap_caps_free(_out[1]);
	heap_caps_free(_cursor);
}

bool Esp32GraphicsManager::hasFeature(OSystem::Feature f) const {
	return f == OSystem::kFeatureCursorPalette;
}

#ifdef USE_RGB_COLOR
Common::List<Graphics::PixelFormat> Esp32GraphicsManager::getSupportedFormats() const {
	Common::List<Graphics::PixelFormat> list;
	list.push_back(Graphics::PixelFormat::createFormatCLUT8());
	return list;
}
#endif

void Esp32GraphicsManager::initSize(uint width, uint height, const Graphics::PixelFormat *format) {
	if (format && format->bytesPerPixel != 1)
		warning("esp32: only CLUT8 screens are supported");
	if (_screen.getPixels() && (uint)_screen.w == width && (uint)_screen.h == height) {
		// (the board forgets the size and palette when a game ends)
		if (!_overlayVisible)
			applyNativeSize();
		_dirty = true;
		return;
	}
	heap_caps_free(_screen.getPixels());
	_screen.init(width, height, width, psramAlloc(width * height), Graphics::PixelFormat::createFormatCLUT8());
	// the composed frames hold the overlay too (RGB565)
	const size_t bytes = MAX<size_t>(width * height, (size_t)_overlay.w * _overlay.h * 2);
	if (bytes > _outBytes) {
		heap_caps_free(_out[0]);
		heap_caps_free(_out[1]);
		_out[0] = (byte *)psramAlloc(bytes);
		_out[1] = (byte *)psramAlloc(bytes);
		_outBytes = bytes;
	}
	_screenChangeId++;
	_dirty = true;
	if (!_overlayVisible)
		applyNativeSize();
}

void Esp32GraphicsManager::applyNativeSize() {
	auto &emu = BoxEmu::get();
	if (_overlayVisible) {
		emu.native_size(_overlay.w, _overlay.h);
		emu.palette(nullptr);
	} else {
		emu.native_size(_screen.w, _screen.h);
		emu.palette(_pal565[_palIndex], 256);
	}
}

void Esp32GraphicsManager::setPalette(const byte *colors, uint start, uint num) {
	assert(start + num <= 256);
	memcpy(_palette + start * 3, colors, num * 3);
	// write the copy the video task isn't reading, then switch to it
	const int next = _palIndex ^ 1;
	for (uint i = 0; i < 256; i++)
		_pal565[next][i] = rgb565(_palette[i * 3], _palette[i * 3 + 1], _palette[i * 3 + 2]);
	_palIndex = next;
	if (!_overlayVisible)
		BoxEmu::get().palette(_pal565[_palIndex], 256);
	_dirty = true;
}

void Esp32GraphicsManager::grabPalette(byte *colors, uint start, uint num) const {
	assert(start + num <= 256);
	memcpy(colors, _palette + start * 3, num * 3);
}

void Esp32GraphicsManager::copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) {
	_screen.copyRectToSurface(buf, pitch, x, y, w, h);
	_dirty = true;
}

void Esp32GraphicsManager::fillScreen(uint32 col) {
	_screen.fillRect(Common::Rect(_screen.w, _screen.h), col);
	_dirty = true;
}

void Esp32GraphicsManager::fillScreen(const Common::Rect &r, uint32 col) {
	_screen.fillRect(r, col);
	_dirty = true;
}

void Esp32GraphicsManager::setShakePos(int shakeXOffset, int shakeYOffset) {
	_shakeX = shakeXOffset;
	_shakeY = shakeYOffset;
	_dirty = true;
}

void Esp32GraphicsManager::updateScreen() {
	present();
}

void Esp32GraphicsManager::refresh() {
	applyNativeSize();
	_dirty = true;
	present();
}

void Esp32GraphicsManager::drawCursor8(byte *dst, int pitch, int w, int h) const {
	const int x0 = _mouseX - _hotX, y0 = _mouseY - _hotY;
	for (uint cy = 0; cy < _cursorH; cy++) {
		const int y = y0 + (int)cy;
		if (y < 0 || y >= h)
			continue;
		const byte *src = _cursor + cy * _cursorW;
		for (uint cx = 0; cx < _cursorW; cx++) {
			const int x = x0 + (int)cx;
			if (x < 0 || x >= w || src[cx] == (byte)_keyColor)
				continue;
			dst[y * pitch + x] = src[cx];
		}
	}
}

void Esp32GraphicsManager::drawCursor16(uint16_t *dst, int pitch, int w, int h) const {
	const byte *pal = _cursorPaletteEnabled ? _cursorPalette : _palette;
	const int x0 = _mouseX - _hotX, y0 = _mouseY - _hotY;
	for (uint cy = 0; cy < _cursorH; cy++) {
		const int y = y0 + (int)cy;
		if (y < 0 || y >= h)
			continue;
		const byte *src = _cursor + cy * _cursorW;
		for (uint cx = 0; cx < _cursorW; cx++) {
			const int x = x0 + (int)cx;
			if (x < 0 || x >= w || src[cx] == (byte)_keyColor)
				continue;
			const byte *c = pal + src[cx] * 3;
			dst[y * pitch + x] = rgb565(c[0], c[1], c[2]);
		}
	}
}

void Esp32GraphicsManager::present() {
	if (!_out[0] || !_dirty)
		return;
	// the display shows ~60 fps at most; a frame pushed while the video task
	// is busy is dropped anyway
	const uint32 now = g_system->getMillis();
	if (now - _lastPresentMs < 15)
		return;
	_lastPresentMs = now;
	_dirty = false;
	byte *out = _out[_outIndex];
	_outIndex ^= 1;
	if (_overlayVisible) {
		uint16_t *dst = (uint16_t *)out;
		memcpy(dst, _overlay.getPixels(), (size_t)_overlay.w * _overlay.h * 2);
		if (_cursorVisible && _cursor)
			drawCursor16(dst, _overlay.w, _overlay.w, _overlay.h);
	} else {
		const int w = _screen.w, h = _screen.h;
		if (_shakeX || _shakeY) {
			memset(out, 0, (size_t)w * h);
			for (int y = 0; y < h; y++) {
				const int sy = y - _shakeY;
				if (sy < 0 || sy >= h)
					continue;
				const int sx0 = MAX(0, -_shakeX), dx0 = MAX(0, _shakeX);
				memcpy(out + y * w + dx0, (const byte *)_screen.getBasePtr(sx0, sy), w - ABS(_shakeX));
			}
		} else {
			memcpy(out, _screen.getPixels(), (size_t)w * h);
		}
		if (_cursorVisible && _cursor)
			drawCursor8(out, w, w, h);
	}
	BoxEmu::get().push_frame(out);
}

size_t Esp32GraphicsManager::snapshotRgb565(uint16_t *dst, size_t maxPixels, int &w, int &h) const {
	if (!_screen.getPixels())
		return 0;
	w = _screen.w;
	h = _screen.h;
	const size_t n = MIN<size_t>(maxPixels, (size_t)w * h);
	const byte *src = (const byte *)_screen.getPixels();
	for (size_t i = 0; i < n; i++) {
		const byte *c = _palette + src[i] * 3;
		dst[i] = rgb565(c[0], c[1], c[2]);
	}
	return n;
}

void Esp32GraphicsManager::showOverlay(bool inGUI) {
	if (_overlayVisible)
		return;
	_overlayVisible = true;
	// the GUI works in overlay coordinates
	_mouseX = _mouseX * _overlay.w / MAX<int>(1, _screen.w);
	_mouseY = _mouseY * _overlay.h / MAX<int>(1, _screen.h);
	applyNativeSize();
	_dirty = true;
}

void Esp32GraphicsManager::hideOverlay() {
	if (!_overlayVisible)
		return;
	_overlayVisible = false;
	_mouseX = _mouseX * MAX<int>(1, _screen.w) / _overlay.w;
	_mouseY = _mouseY * MAX<int>(1, _screen.h) / _overlay.h;
	applyNativeSize();
	_dirty = true;
}

Graphics::PixelFormat Esp32GraphicsManager::getOverlayFormat() const {
	return kRgb565;
}

void Esp32GraphicsManager::clearOverlay() {
	memset(_overlay.getPixels(), 0, (size_t)_overlay.w * _overlay.h * 2);
	_dirty = true;
}

void Esp32GraphicsManager::grabOverlay(Graphics::Surface &surface) const {
	assert(surface.w >= _overlay.w && surface.h >= _overlay.h);
	for (int y = 0; y < _overlay.h; y++)
		memcpy(surface.getBasePtr(0, y), _overlay.getBasePtr(0, y), _overlay.w * 2);
}

void Esp32GraphicsManager::copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) {
	_overlay.copyRectToSurface(buf, pitch, x, y, w, h);
	_dirty = true;
}

bool Esp32GraphicsManager::showMouse(bool visible) {
	const bool last = _cursorVisible;
	_cursorVisible = visible;
	_dirty = true;
	return last;
}

void Esp32GraphicsManager::warpMouse(int x, int y) {
	_mouseX = CLIP(x, 0, MAX(0, activeWidth() - 1));
	_mouseY = CLIP(y, 0, MAX(0, activeHeight() - 1));
	_dirty = true;
}

void Esp32GraphicsManager::setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
                                          const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) {
	if (format && format->bytesPerPixel != 1) {
		warning("esp32: only CLUT8 cursors are supported");
		return;
	}
	if (w * h > _cursorW * _cursorH || !_cursor) {
		heap_caps_free(_cursor);
		_cursor = (byte *)psramAlloc(MAX<uint>(1, w * h));
	}
	memcpy(_cursor, buf, w * h);
	if (mask) {
		// pixels the mask leaves out become the key color
		for (uint i = 0; i < w * h; i++)
			if (!mask[i])
				_cursor[i] = (byte)keycolor;
	}
	_cursorW = w;
	_cursorH = h;
	_hotX = hotspotX;
	_hotY = hotspotY;
	_keyColor = keycolor;
	_dirty = true;
}

void Esp32GraphicsManager::setCursorPalette(const byte *colors, uint start, uint num) {
	assert(start + num <= 256);
	memcpy(_cursorPalette + start * 3, colors, num * 3);
	_cursorPaletteEnabled = true;
	_dirty = true;
}
