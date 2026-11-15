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

#ifndef BACKENDS_PLATFORM_ESP32_GRAPHICS_H
#define BACKENDS_PLATFORM_ESP32_GRAPHICS_H

#include "backends/graphics/graphics.h"
#include "graphics/surface.h"

// The game screen (CLUT8) and the GUI overlay (RGB565) of the esp-cpp
// emulators (tab5-emu): each update composes the mouse cursor in and hands
// the frame to the board's video task (BoxEmu::push_frame()), which scales it
// to the display.
class Esp32GraphicsManager : public GraphicsManager {
public:
	Esp32GraphicsManager();
	~Esp32GraphicsManager() override;

	bool hasFeature(OSystem::Feature f) const override;
	void setFeatureState(OSystem::Feature f, bool enable) override {}
	bool getFeatureState(OSystem::Feature f) const override { return false; }

#ifdef USE_RGB_COLOR
	Graphics::PixelFormat getScreenFormat() const override { return Graphics::PixelFormat::createFormatCLUT8(); }
	Common::List<Graphics::PixelFormat> getSupportedFormats() const override;
#endif

	void initSize(uint width, uint height, const Graphics::PixelFormat *format = nullptr) override;
	int getScreenChangeID() const override { return _screenChangeId; }

	void beginGFXTransaction() override {}
	OSystem::TransactionError endGFXTransaction() override { return OSystem::kTransactionSuccess; }

	int16 getHeight() const override { return _screen.h; }
	int16 getWidth() const override { return _screen.w; }
	void setPalette(const byte *colors, uint start, uint num) override;
	void grabPalette(byte *colors, uint start, uint num) const override;
	void copyRectToScreen(const void *buf, int pitch, int x, int y, int w, int h) override;
	Graphics::Surface *lockScreen() override { return &_screen; }
	void unlockScreen() override {}
	void fillScreen(uint32 col) override;
	void fillScreen(const Common::Rect &r, uint32 col) override;
	void updateScreen() override;
	void setShakePos(int shakeXOffset, int shakeYOffset) override;
	void setFocusRectangle(const Common::Rect &rect) override {}
	void clearFocusRectangle() override {}

	void showOverlay(bool inGUI) override;
	void hideOverlay() override;
	bool isOverlayVisible() const override { return _overlayVisible; }
	Graphics::PixelFormat getOverlayFormat() const override;
	void clearOverlay() override;
	void grabOverlay(Graphics::Surface &surface) const override;
	void copyRectToOverlay(const void *buf, int pitch, int x, int y, int w, int h) override;
	int16 getOverlayHeight() const override { return _overlay.h; }
	int16 getOverlayWidth() const override { return _overlay.w; }

	bool showMouse(bool visible) override;
	void warpMouse(int x, int y) override;
	void setMouseCursor(const void *buf, uint w, uint h, int hotspotX, int hotspotY, uint32 keycolor,
	                    const Graphics::PixelFormat *format, const byte *mask, frac_t scaleX, frac_t scaleY) override;
	void setCursorPalette(const byte *colors, uint start, uint num) override;

	/// The mouse position in the current (game or overlay) coordinates
	int mouseX() const { return _mouseX; }
	int mouseY() const { return _mouseY; }
	/// Size of what is shown now (the overlay when it is up)
	int activeWidth() const { return _overlayVisible ? _overlay.w : _screen.w; }
	int activeHeight() const { return _overlayVisible ? _overlay.h : _screen.h; }

	/// The last presented frame as RGB565 (for the save slot screenshots);
	/// returns the number of pixels written (0 if there is none yet)
	size_t snapshotRgb565(uint16_t *dst, size_t maxPixels, int &w, int &h) const;
	/// Re-send the palette and the frame (the board's menu drew over them)
	void refresh();
	/// Free the screen, overlay, frame and cursor buffers (a game ended: the
	/// next one makes them again)
	void release();

private:
	void present();
	void drawCursor8(byte *dst, int pitch, int w, int h) const;
	void drawCursor16(uint16_t *dst, int pitch, int w, int h) const;
	void applyNativeSize();
	bool ensureOut(size_t bytes);
	bool ensureOverlay();

	Graphics::Surface _screen;   // CLUT8, the game's
	Graphics::Surface _overlay;  // RGB565, the GUI's
	byte *_out[2] = {nullptr, nullptr}; // composed frames (alternating)
	size_t _outBytes = 0;
	int _outIndex = 0;
	byte _palette[256 * 3] = {};
	uint16_t _pal565[2][256] = {}; // the video task reads one while the other is written
	int _palIndex = 0;
	byte _cursorPalette[256 * 3] = {};
	bool _cursorPaletteEnabled = false;
	byte *_cursor = nullptr;
	uint _cursorW = 0, _cursorH = 0;
	int _hotX = 0, _hotY = 0;
	uint32 _keyColor = 0;
	bool _cursorVisible = false;
	int _mouseX = 0, _mouseY = 0;
	int _shakeX = 0, _shakeY = 0;
	bool _overlayVisible = false;
	int _screenChangeId = 0;
	uint32 _lastPresentMs = 0;
	bool _dirty = true;
};

#endif
