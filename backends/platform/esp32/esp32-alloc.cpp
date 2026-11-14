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

// ScummVM's allocations. It makes many small ones (paths, strings, hash map
// nodes: an index of a game's files alone is thousands), which ESP-IDF's
// malloc puts in the scarce internal RAM. The build renames ScummVM's
// malloc / calloc / realloc / operator new to these (psram_alloc.syms), which
// prefer PSRAM; free() and delete take either.
//
// With SCUMMVM_ESP32_TRACK_ALLOCS (and free / delete renamed too:
// psram_alloc_track.syms), every allocation is recorded with its callers and
// the game run it was made in; scummvm_alloc_report() writes what a run left
// allocated, grouped by call stack (decode the addresses with addr2line).

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>

#include "scummvm_port.hpp"

static constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

#ifdef SCUMMVM_ESP32_TRACK_ALLOCS

namespace {

constexpr int kFrames = 8;
constexpr uint32_t kSlots = 1u << 15; // live allocations tracked at once

struct Slot {
	void *ptr; // nullptr: free, kTombstone: deleted
	uint32_t size;
	uint32_t gen;
	uint32_t pc[kFrames];
};
void *const kTombstone = (void *)1;

Slot *s_slots = nullptr;
uint32_t s_gen = 0;
uint32_t s_overflow = 0;
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

inline uint32_t hashPtr(void *p) { return ((uint32_t)(uintptr_t)p >> 3) * 2654435761u; }

bool plausibleFp(uint32_t fp) {
	return (fp & 3) == 0 && ((fp >= 0x48000000 && fp < 0x4c000000) || (fp >= 0x4ff00000 && fp < 0x4ffc0000));
}

// RISC-V with frame pointers: ra at fp-4, the caller's fp at fp-8
void callers(uint32_t *pc) {
	uint32_t fp = (uint32_t)(uintptr_t)__builtin_frame_address(0);
	for (int i = 0; i < kFrames; i++) {
		if (!plausibleFp(fp)) {
			pc[i] = 0;
			continue;
		}
		pc[i] = *(uint32_t *)(fp - 4);
		fp = *(uint32_t *)(fp - 8);
	}
}

void track(void *p, size_t size) {
	if (!p)
		return;
	if (!s_slots) {
		s_slots = (Slot *)heap_caps_calloc(kSlots, sizeof(Slot), kPsram);
		if (!s_slots)
			return;
	}
	Slot rec;
	rec.ptr = p;
	rec.size = size;
	callers(rec.pc);
	portENTER_CRITICAL(&s_lock);
	rec.gen = s_gen;
	uint32_t i = hashPtr(p) & (kSlots - 1);
	for (uint32_t n = 0; n < kSlots; n++, i = (i + 1) & (kSlots - 1)) {
		if (s_slots[i].ptr == nullptr || s_slots[i].ptr == kTombstone) {
			s_slots[i] = rec;
			portEXIT_CRITICAL(&s_lock);
			return;
		}
	}
	s_overflow++;
	portEXIT_CRITICAL(&s_lock);
}

void untrack(void *p) {
	if (!p || !s_slots)
		return;
	portENTER_CRITICAL(&s_lock);
	uint32_t i = hashPtr(p) & (kSlots - 1);
	for (uint32_t n = 0; n < kSlots && s_slots[i].ptr; n++, i = (i + 1) & (kSlots - 1)) {
		if (s_slots[i].ptr == p) {
			s_slots[i].ptr = kTombstone;
			break;
		}
	}
	portEXIT_CRITICAL(&s_lock);
}

} // namespace

void scummvm_alloc_new_generation() {
	portENTER_CRITICAL(&s_lock);
	s_gen++;
	portEXIT_CRITICAL(&s_lock);
}

void scummvm_alloc_report(const char *path) {
	if (!s_slots)
		return;
	// group the run's live allocations by call stack (no allocations here
	// that would be tracked: heap_caps_* only)
	struct Group {
		uint32_t pc[kFrames];
		uint32_t count, bytes;
	};
	constexpr uint32_t kGroups = 2048;
	Group *groups = (Group *)heap_caps_calloc(kGroups, sizeof(Group), kPsram);
	if (!groups)
		return;
	uint32_t used = 0, live = 0, liveBytes = 0, allLive = 0;
	portENTER_CRITICAL(&s_lock);
	const uint32_t gen = s_gen;
	for (uint32_t i = 0; i < kSlots; i++) {
		const Slot &s = s_slots[i];
		if (!s.ptr || s.ptr == kTombstone)
			continue;
		allLive++;
		if (s.gen != gen)
			continue;
		live++;
		liveBytes += s.size;
		uint32_t g = 0;
		for (; g < used; g++)
			if (!memcmp(groups[g].pc, s.pc, sizeof(s.pc)))
				break;
		if (g == used) {
			if (used == kGroups)
				continue;
			memcpy(groups[used].pc, s.pc, sizeof(s.pc));
			used++;
		}
		groups[g].count++;
		groups[g].bytes += s.size;
	}
	portEXIT_CRITICAL(&s_lock);
	// biggest first
	for (uint32_t a = 0; a < used; a++)
		for (uint32_t b = a + 1; b < used; b++)
			if (groups[b].bytes > groups[a].bytes) {
				const Group t = groups[a];
				groups[a] = groups[b];
				groups[b] = t;
			}
	FILE *f = fopen(path, "a");
	if (f) {
		fprintf(f, "run %u: %u allocations (%u bytes) still live; %u live in all; %u untracked (table full)\n",
		        (unsigned)gen, (unsigned)live, (unsigned)liveBytes, (unsigned)allLive, (unsigned)s_overflow);
		for (uint32_t g = 0; g < used; g++) {
			fprintf(f, "%u %u", (unsigned)groups[g].bytes, (unsigned)groups[g].count);
			for (int k = 0; k < kFrames; k++)
				fprintf(f, " 0x%08x", (unsigned)groups[g].pc[k]);
			fprintf(f, "\n");
		}
		fclose(f);
	}
	printf("[scummvm] run %u left %u allocations (%u bytes): %s\n", (unsigned)gen, (unsigned)live, (unsigned)liveBytes, path);
	heap_caps_free(groups);
}

extern "C" void scummvm_psram_free(void *p) {
	untrack(p);
	heap_caps_free(p);
}

void scummvm_psram_delete(void *p) asm("scummvm_psram_delete");
void scummvm_psram_delete(void *p) { scummvm_psram_free(p); }
void scummvm_psram_delete_sized(void *p, size_t) asm("scummvm_psram_delete_sized");
void scummvm_psram_delete_sized(void *p, size_t) { scummvm_psram_free(p); }
void scummvm_psram_delete_array(void *p) asm("scummvm_psram_delete_array");
void scummvm_psram_delete_array(void *p) { scummvm_psram_free(p); }
void scummvm_psram_delete_array_sized(void *p, size_t) asm("scummvm_psram_delete_array_sized");
void scummvm_psram_delete_array_sized(void *p, size_t) { scummvm_psram_free(p); }

#define TRACK(p, size) track(p, size)
#define UNTRACK(p) untrack(p)

#else

void scummvm_alloc_new_generation() {}
void scummvm_alloc_report(const char *) {}

#define TRACK(p, size)
#define UNTRACK(p)

#endif

extern "C" void *scummvm_psram_malloc(size_t size) {
	void *p = heap_caps_malloc(size, kPsram);
	if (!p)
		p = heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
	TRACK(p, size);
	return p;
}

extern "C" void *scummvm_psram_calloc(size_t n, size_t size) {
	void *p = heap_caps_calloc(n, size, kPsram);
	if (!p)
		p = heap_caps_calloc(n, size, MALLOC_CAP_DEFAULT);
	TRACK(p, n * size);
	return p;
}

extern "C" void *scummvm_psram_realloc(void *ptr, size_t size) {
	UNTRACK(ptr);
	void *p = heap_caps_realloc(ptr, size, kPsram);
	if (!p && size != 0)
		p = heap_caps_realloc(ptr, size, MALLOC_CAP_DEFAULT);
	TRACK(p ? p : (size ? ptr : nullptr), size); // (a failed realloc keeps ptr)
	return p;
}

void *scummvm_psram_new(size_t size) asm("scummvm_psram_new");
void *scummvm_psram_new(size_t size) {
	void *p = scummvm_psram_malloc(size ? size : 1);
	if (!p)
		abort(); // (no exceptions)
	return p;
}

void *scummvm_psram_new_array(size_t size) asm("scummvm_psram_new_array");
void *scummvm_psram_new_array(size_t size) { return scummvm_psram_new(size); }

