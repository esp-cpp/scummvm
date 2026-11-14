/* ScummVM configuration for the esp-cpp emulators (ESP32-P4, ESP-IDF):
 * written by hand from what ./configure makes for a one-engine build */
#ifndef CONFIG_H
#define CONFIG_H

#define SCUMM_LITTLE_ENDIAN
#undef SCUMM_BIG_ENDIAN
#define SCUMM_NEED_ALIGNMENT

#undef USE_ELF_LOADER
#undef DYNAMIC_MODULES
#define DETECTION_STATIC
#undef DETECTION_FULL
#undef USE_MT32EMU
#undef DISABLE_NUKED_OPL
#define DISABLE_NUKED_OPL /* the DOSBox OPL emulator is enough (and lighter) */
#define USE_RGB_COLOR
#undef USE_HIGHRES
#define USE_SAVEGAME_TIMESTAMP
#undef USE_SCALERS
#undef USE_HQ_SCALERS
#undef USE_EDGE_SCALERS
#undef USE_ASPECT
#undef USE_OGG
#undef USE_VORBIS
#undef USE_TREMOR
#undef USE_FLAC
#undef USE_MAD
#undef USE_JPEG
#undef USE_PNG
#undef USE_FAAD
#undef USE_ZLIB
#undef USE_A52
#undef USE_FLUIDSYNTH
#undef USE_FREETYPE2
#undef USE_OPENGL
#undef USE_OPENGL_GAME
#undef USE_OPENGL_SHADERS
#undef USE_FRIBIDI
#undef USE_TIMIDITY
#undef USE_TEXT_CONSOLE_FOR_DEBUGGER
#undef ENABLE_VKEYBD
#undef ENABLE_EVENTRECORDER
#undef USE_TRANSLATION
#undef USE_TASKBAR
#undef USE_SYSDIALOGS
#undef USE_TTS
#undef USE_BINK
#undef USE_UPDATES
#undef USE_CLOUD
#undef USE_LIBCURL
#undef USE_DISCORD
#define BUILTIN_RESOURCES
#define USE_MIDI
/* no ScummVM GUI (gui/): its few entry points are stubbed by the backend
 * (backends/platform/esp32/gui-stubs.cpp) */
#define SCUMMVM_NO_GUI

#endif /* CONFIG_H */
