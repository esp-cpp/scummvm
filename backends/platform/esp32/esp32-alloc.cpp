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

// ScummVM's allocations. The build renames ScummVM's malloc / calloc /
// realloc / free / operator new / delete to these (psram_alloc.syms):
// - they prefer PSRAM: ScummVM makes many small allocations (paths, strings,
//   hash map nodes: an index of a game's files alone is thousands), which
//   ESP-IDF's malloc would put in the scarce internal RAM;
// - while a game runs they come from an arena (scummvm_arena_begin()): one
//   PSRAM block given back whole when the game ends, so that a game leaves no
//   allocations scattered over the heap (the board's other games want big
//   contiguous blocks).
//
// With SCUMMVM_ESP32_TRACK_ALLOCS, every allocation is also recorded with
// its callers and the game run it was made in; scummvm_alloc_report() writes
// what a run left allocated, grouped by call stack (decode the addresses with
// addr2line).

#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <multi_heap.h>

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

static bool inArenaRange(const void *p);

void scummvm_alloc_report(const char *path) {
	if (!s_slots)
		return;
	// group the run's live allocations by call stack (no allocations here
	// that would be tracked: heap_caps_* only)
	struct Group {
		uint32_t pc[kFrames];
		uint32_t count, bytes, inArena;
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
		if (inArenaRange(s.ptr))
			groups[g].inArena++;
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
			fprintf(f, "%u %u%s", (unsigned)groups[g].bytes, (unsigned)groups[g].count, groups[g].inArena ? "A" : "");
			for (int k = 0; k < kFrames; k++)
				fprintf(f, " 0x%08x", (unsigned)groups[g].pc[k]);
			fprintf(f, "\n");
		}
		fclose(f);
	}
	printf("[scummvm] run %u left %u allocations (%u bytes): %s\n", (unsigned)gen, (unsigned)live, (unsigned)liveBytes, path);
	heap_caps_free(groups);
}

#define TRACK(p, size) track(p, size)
#define UNTRACK(p) untrack(p)

#else

void scummvm_alloc_new_generation() {}
void scummvm_alloc_report(const char *) {}

#define TRACK(p, size)
#define UNTRACK(p)

#endif

// The arena of the running game (none: the heap)
namespace {
multi_heap_handle_t s_arena = nullptr;
bool s_arenaAllocating = false; // (frees go back to the arena until it's gone)
uintptr_t s_arenaBegin = 0, s_arenaEnd = 0;
portMUX_TYPE s_arenaLock = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_arenaMisses = 0; // allocations the arena had no room for

inline bool inArena(const void *p) {
	return (uintptr_t)p >= s_arenaBegin && (uintptr_t)p < s_arenaEnd;
}

void *heapAlloc(size_t size) {
	if (s_arenaAllocating) {
		if (void *p = multi_heap_malloc(s_arena, size))
			return p;
		s_arenaMisses++;
	}
	void *p = heap_caps_malloc(size, kPsram);
	return p ? p : heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
}
} // namespace

bool scummvm_arena_begin(void *block, size_t size) {
	multi_heap_handle_t h = multi_heap_register(block, size);
	if (!h)
		return false;
	multi_heap_set_lock(h, &s_arenaLock);
	s_arenaBegin = (uintptr_t)block;
	s_arenaEnd = s_arenaBegin + size;
	s_arenaMisses = 0;
	s_arena = h;
	s_arenaAllocating = true;
	return true;
}

void scummvm_arena_stop() {
	s_arenaAllocating = false;
}

size_t scummvm_arena_end() {
	s_arenaAllocating = false;
	if (!s_arena)
		return 0;
	multi_heap_info_t info;
	multi_heap_get_info(s_arena, &info);
	printf("[scummvm] arena: peak %u KB used, %u blocks left (%u B), %u allocations outside\n",
	       (unsigned)((s_arenaEnd - s_arenaBegin - info.minimum_free_bytes) / 1024), (unsigned)info.allocated_blocks,
	       (unsigned)info.total_allocated_bytes, (unsigned)s_arenaMisses);
	s_arena = nullptr;
	// (the range stays known: a stale pointer into it is never handed to
	// the heap's free())
	return info.allocated_blocks;
}

extern "C" void *scummvm_psram_malloc(size_t size) {
	void *p = heapAlloc(size);
	TRACK(p, size);
	return p;
}

extern "C" void *scummvm_psram_calloc(size_t n, size_t size) {
	const size_t bytes = n * size;
	void *p = heapAlloc(bytes);
	if (p)
		memset(p, 0, bytes);
	TRACK(p, bytes);
	return p;
}

extern "C" void scummvm_psram_free(void *p) {
	if (!p)
		return;
	UNTRACK(p);
	if (inArena(p)) {
		if (s_arena)
			multi_heap_free(s_arena, p);
		return; // (an arena that's gone: nothing to free)
	}
	heap_caps_free(p);
}

extern "C" void *scummvm_psram_realloc(void *ptr, size_t size) {
	if (!ptr)
		return scummvm_psram_malloc(size);
	if (size == 0) {
		scummvm_psram_free(ptr);
		return nullptr;
	}
	if (!inArena(ptr)) {
		UNTRACK(ptr);
		void *p = heap_caps_realloc(ptr, size, kPsram);
		if (!p)
			p = heap_caps_realloc(ptr, size, MALLOC_CAP_DEFAULT);
		TRACK(p ? p : ptr, size); // (a failed realloc keeps ptr)
		return p;
	}
	if (s_arenaAllocating) {
		UNTRACK(ptr);
		if (void *p = multi_heap_realloc(s_arena, ptr, size)) {
			TRACK(p, size);
			return p;
		}
		TRACK(ptr, size);
	}
	// the arena is full (or gone): move it out
	const size_t old = s_arena ? multi_heap_get_allocated_size(s_arena, ptr) : size;
	void *p = scummvm_psram_malloc(size);
	if (p) {
		memcpy(p, ptr, old < size ? old : size);
		scummvm_psram_free(ptr);
	}
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

void scummvm_psram_delete(void *p) asm("scummvm_psram_delete");
void scummvm_psram_delete(void *p) { scummvm_psram_free(p); }
void scummvm_psram_delete_sized(void *p, size_t) asm("scummvm_psram_delete_sized");
void scummvm_psram_delete_sized(void *p, size_t) { scummvm_psram_free(p); }
void scummvm_psram_delete_array(void *p) asm("scummvm_psram_delete_array");
void scummvm_psram_delete_array(void *p) { scummvm_psram_free(p); }
void scummvm_psram_delete_array_sized(void *p, size_t) asm("scummvm_psram_delete_array_sized");
void scummvm_psram_delete_array_sized(void *p, size_t) { scummvm_psram_free(p); }

#ifdef SCUMMVM_ESP32_TRACK_ALLOCS
static bool inArenaRange(const void *p) { return inArena(p); }
#endif


