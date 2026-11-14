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

// The parts of ScummVM's GUI (gui/) that engines and the core call, for a
// build without it (SCUMMVM_NO_GUI; the GUI is ~500 KB of code): a message
// dialog prints its message and answers with its default button, the
// save / load chooser is cancelled (the board's pause menu saves and loads),
// and the debugger console does nothing.

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "common/scummsys.h"

#include <cstdarg>
#include <cstdio>

#include "gui/debugger.h"
#include "gui/dialog.h"
#include "gui/gui-manager.h"
#include "gui/message.h"
#include "gui/object.h"
#include "gui/saveload.h"

namespace Common {
DECLARE_SINGLETON(GUI::GuiManager);
}

namespace GUI {

// GuiManager: never active (no dialog is ever shown)
GuiManager::GuiManager() : CommandSender(nullptr) { _launched = false; }
GuiManager::~GuiManager() {}
void GuiManager::initTextToSpeech() {}

// GuiObject / Dialog: no widgets, no drawing
GuiObject::GuiObject(int x, int y, int w, int h, bool scale) : _x(x), _y(y), _w(w), _h(h), _useRTL(false), _firstWidget(nullptr) {}
GuiObject::GuiObject(const Common::String &name) : _x(0), _y(0), _w(0), _h(0), _useRTL(false), _name(name), _firstWidget(nullptr) {}
GuiObject::~GuiObject() {}
void GuiObject::resize(int x, int y, int w, int h, bool scale) {}
Widget *GuiObject::addChild(Widget *newChild) { return newChild; }
void GuiObject::reflowLayout() {}
void GuiObject::removeWidget(Widget *widget) {}
Common::Rect GuiObject::getClipRect() const { return Common::Rect(); }

Dialog::Dialog(int x, int y, int w, int h, bool scale)
	: GuiObject(x, y, w, h, scale), _mouseWidget(nullptr), _focusedWidget(nullptr), _dragWidget(nullptr),
	  _dragHookWidget(nullptr), _tickleWidget(nullptr), _visible(false), _backgroundType(ThemeEngine::kDialogBackgroundDefault),
	  _result(kMessageOK), _handlingMouseWheel(false) {}
Dialog::Dialog(const Common::String &name)
	: GuiObject(name), _mouseWidget(nullptr), _focusedWidget(nullptr), _dragWidget(nullptr),
	  _dragHookWidget(nullptr), _tickleWidget(nullptr), _visible(false), _backgroundType(ThemeEngine::kDialogBackgroundDefault),
	  _result(kMessageOK), _handlingMouseWheel(false) {}
int Dialog::runModal() { return _result; }
void Dialog::releaseFocus() {}
void Dialog::reflowLayout() {}
void Dialog::lostFocus() {}
void Dialog::open() {}
void Dialog::close() {}
void Dialog::drawDialog(DrawLayer layerToDraw, bool resetClipping) {}
void Dialog::handleTickle() {}
void Dialog::handleMouseDown(int x, int y, int button, int clickCount) {}
void Dialog::handleMouseUp(int x, int y, int button, int clickCount) {}
void Dialog::handleMouseWheel(int x, int y, int direction) {}
void Dialog::handleKeyDown(Common::KeyState state) {}
void Dialog::handleKeyUp(Common::KeyState state) {}
void Dialog::handleMouseMoved(int x, int y, int button) {}
void Dialog::handleOtherEvent(const Common::Event &evt) {}
void Dialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {}
Widget *Dialog::findWidget(const char *name) { return nullptr; }
void Dialog::removeWidget(Widget *widget) {}
void Dialog::registerTickleWidget(Widget *widget) {}
void Dialog::unregisterTickleWidget(Widget *widget) {}

// MessageDialog: the message goes to the log; the answer is the default button
static void logMessage(const Common::U32String &message) {
	printf("[scummvm] %s\n", message.encode().c_str());
}
MessageDialog::MessageDialog(const Common::U32String &message) : Dialog(0, 0, 0, 0), _url(nullptr), _message(message), _alignment(Graphics::kTextAlignCenter), _extraMessage(nullptr) {
	logMessage(message);
}
MessageDialog::MessageDialog(const Common::String &message) : MessageDialog(Common::U32String(message)) {}
MessageDialog::MessageDialog(const Common::U32String &message, const Common::U32String &defaultButton, const Common::U32String &altButton,
                             Graphics::TextAlign alignment, const char *url, const Common::U32String &extraMessage)
	: MessageDialog(message) {}
MessageDialog::MessageDialog(const Common::String &message, const Common::String &defaultButton, const Common::String &altButton,
                             Graphics::TextAlign alignment, const char *url)
	: MessageDialog(Common::U32String(message)) {}
MessageDialog::MessageDialog(const Common::U32String &message, const Common::U32String &defaultButton,
                             const Common::U32StringArray &altButtons, Graphics::TextAlign alignment)
	: MessageDialog(message) {}
void MessageDialog::handleCommand(CommandSender *sender, uint32 cmd, uint32 data) {}
void MessageDialog::reflowLayout() {}

TimedMessageDialog::TimedMessageDialog(const Common::U32String &message, uint32 duration) : MessageDialog(message), _timer(0) {}
void TimedMessageDialog::handleTickle() {}

MessageDialogWithURL::MessageDialogWithURL(const Common::U32String &message, const char *url) : MessageDialog(message) {}

// SaveLoadChooser: cancelled
SaveLoadChooser::SaveLoadChooser(bool saveMode) : _impl(nullptr), _saveMode(saveMode) {}
SaveLoadChooser::~SaveLoadChooser() {}
int SaveLoadChooser::runModalWithCurrentTarget() { return -1; }
const Common::String SaveLoadChooser::getResultString() const { return Common::String(); }
Common::String SaveLoadChooser::createDefaultSaveDescription(const int slot) const {
	return Common::String::format("Save %d", slot);
}

// Debugger: no console; output goes to the log
Debugger::Debugger() : _frameCountdown(0), _isActive(false), _firstTime(true), _debuggerDialog(nullptr) {}
Debugger::~Debugger() {}
int Debugger::debugPrintf(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	const int n = vprintf(format, ap);
	va_end(ap);
	return n;
}
void Debugger::onFrame() {}
void Debugger::attach(const char *entry) {}
void Debugger::preEnter() {}
void Debugger::postEnter() {}
bool Debugger::handleCommand(int argc, const char **argv, bool &keepRunning) { return false; }
void Debugger::registerCmd(const Common::String &cmdname, Debuglet *debuglet) { delete debuglet; }

} // namespace GUI
