/*
// Copyright (c) 2026 AMI
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

// ============================================================
// Mock implementation of libgpiod C functions
//
// These definitions override the real libgpiod.so symbols via ELF
// symbol interposition — the main executable's symbols take priority
// over shared library symbols resolved through PLT.
// ============================================================

#include "mock_gpiod.hpp"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// ============================================================
// Internal mock state
// ============================================================
namespace
{

struct MockLineState
{
    std::string name;
    int value = 0;
    bool requested = false;
};

struct MockState
{
    std::mutex mtx;
    std::map<std::string, MockLineState> lines;
    int eventFd = -1;
    bool eventFdOwned = false;
    std::function<void()> eventReadCallback;
    bool failEventRead = false;
    bool failFindLine = false;
    bool failLineRequest = false;
    bool failEventGetFd = false;
};

MockState& state()
{
    static MockState s;
    return s;
}

// Ensure we have a valid eventfd
int ensureEventFd()
{
    auto& s = state();
    if (s.eventFd < 0)
    {
        s.eventFd = eventfd(0, EFD_NONBLOCK);
        s.eventFdOwned = true;
    }
    return s.eventFd;
}

} // anonymous namespace

// ============================================================
// MockGPIO namespace implementation
// ============================================================
namespace MockGPIO
{

void setLineValue(const std::string& name, int value)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.lines[name].value = value;
}

int getLineValue(const std::string& name)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto it = s.lines.find(name);
    if (it != s.lines.end())
    {
        return it->second.value;
    }
    return 0;
}

int getEventFd()
{
    return ensureEventFd();
}

void setEventFd(int fd)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    if (s.eventFdOwned && s.eventFd >= 0)
    {
        close(s.eventFd);
    }
    s.eventFd = fd;
    s.eventFdOwned = false;
}

void reset()
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.lines.clear();
    if (s.eventFdOwned && s.eventFd >= 0)
    {
        close(s.eventFd);
    }
    s.eventFd = -1;
    s.eventFdOwned = false;
    s.eventReadCallback = nullptr;
    s.failEventRead = false;
    s.failFindLine = false;
    s.failLineRequest = false;
    s.failEventGetFd = false;
}

bool wasLineRequested(const std::string& name)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto it = s.lines.find(name);
    return it != s.lines.end() && it->second.requested;
}

std::vector<std::string> getRegisteredLines()
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    std::vector<std::string> result;
    for (const auto& [name, lineState] : s.lines)
    {
        result.push_back(name);
    }
    return result;
}

void setEventReadCallback(std::function<void()> cb)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.eventReadCallback = std::move(cb);
}

void setFailEventRead(bool fail)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.failEventRead = fail;
}

void setFailFindLine(bool fail)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.failFindLine = fail;
}

void setFailLineRequest(bool fail)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.failLineRequest = fail;
}

void setFailEventGetFd(bool fail)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.failEventGetFd = fail;
}

} // namespace MockGPIO

// ============================================================
// Fake internal structs (opaque in real libgpiod)
// ============================================================

// These must match what libgpiodcxx expects. Since the C++ wrapper
// stores a gpiod_line* and gpiod_chip* as opaque pointers and only
// passes them to C API functions, we can define minimal fakes.

struct gpiod_chip
{
    char name[64];
    char label[64];
    unsigned int numLines;
};

struct gpiod_line
{
    struct gpiod_chip* chip;
    unsigned int offset;
    char name[64];
    char consumer[64];
    int direction;
    int activeState;
    bool isRequested;
    int value;
};

struct gpiod_chip_iter
{
    bool done;
    struct gpiod_chip* fakeChip;
};

// Public structs from gpiod.h — these are NOT opaque
#ifndef GPIOD_LINE_BULK_MAX_LINES
#define GPIOD_LINE_BULK_MAX_LINES 64
#endif

struct gpiod_line_bulk
{
    struct gpiod_line* lines[GPIOD_LINE_BULK_MAX_LINES];
    unsigned int num_lines;
};

struct gpiod_line_request_config
{
    const char* consumer;
    int request_type;
    int flags;
};

struct gpiod_line_event
{
    struct timespec ts;
    int event_type;
};

// Global fake chip (reused)
static gpiod_chip fakeChip = {"gpiochip0", "mock", 64};

// Line pool — each unique name gets a persistent gpiod_line
// Uses a deque to avoid reallocations and provide stable addresses
static std::deque<gpiod_line>& lineStorage()
{
    static std::deque<gpiod_line> storage;
    return storage;
}

static std::map<std::string, gpiod_line*>& linePool()
{
    static std::map<std::string, gpiod_line*> pool;
    return pool;
}

static gpiod_line* getOrCreateLine(const char* name)
{
    auto& pool = linePool();
    auto it = pool.find(name);
    if (it != pool.end())
    {
        return it->second;
    }
    auto& storage = lineStorage();
    storage.emplace_back();
    auto* line = &storage.back();
    line->chip = &fakeChip;
    line->offset = static_cast<unsigned int>(pool.size());
    strncpy(line->name, name, sizeof(line->name) - 1);
    line->name[sizeof(line->name) - 1] = '\0';
    line->direction = 1; // INPUT
    line->activeState = 1;
    line->isRequested = false;
    line->value = 0;
    pool[name] = line;

    // Also register in MockGPIO state
    auto& s = state();
    std::lock_guard lock(s.mtx);
    s.lines[name].name = name;

    return line;
}

// ============================================================
// C-linkage gpiod function overrides
// ============================================================

extern "C"
{
// --- Chip iterator ---

struct gpiod_chip_iter* gpiod_chip_iter_new(void)
{
    auto* iter = new gpiod_chip_iter{};
    iter->done = false;
    iter->fakeChip = &fakeChip;
    return iter;
}

void gpiod_chip_iter_free_noclose(struct gpiod_chip_iter* iter)
{
    delete iter;
}

struct gpiod_chip* gpiod_chip_iter_next_noclose(struct gpiod_chip_iter* iter)
{
    if (!iter || iter->done)
    {
        return nullptr;
    }
    iter->done = true;
    return iter->fakeChip;
}

// --- Chip operations ---

struct gpiod_chip* gpiod_chip_open(const char* /*path*/)
{
    return &fakeChip;
}

struct gpiod_chip* gpiod_chip_open_lookup(const char* /*descr*/)
{
    return &fakeChip;
}

struct gpiod_chip* gpiod_chip_open_by_name(const char* /*name*/)
{
    return &fakeChip;
}

struct gpiod_chip* gpiod_chip_open_by_number(unsigned int /*num*/)
{
    return &fakeChip;
}

struct gpiod_chip* gpiod_chip_open_by_label(const char* /*label*/)
{
    return &fakeChip;
}

void gpiod_chip_close(struct gpiod_chip* /*chip*/)
{
    // no-op for our fake chip
}

const char* gpiod_chip_name(struct gpiod_chip* chip)
{
    return chip ? chip->name : "mock";
}

const char* gpiod_chip_label(struct gpiod_chip* chip)
{
    return chip ? chip->label : "mock";
}

unsigned int gpiod_chip_num_lines(struct gpiod_chip* chip)
{
    return chip ? chip->numLines : 64;
}

struct gpiod_line* gpiod_chip_get_line(struct gpiod_chip* /*chip*/,
                                       unsigned int offset)
{
    // Create a line with a generated name
    char name[64];
    snprintf(name, sizeof(name), "line_%u", offset);
    return getOrCreateLine(name);
}

struct gpiod_line* gpiod_chip_find_line(struct gpiod_chip* /*chip*/,
                                        const char* name)
{
    if (!name)
    {
        return nullptr;
    }
    {
        auto& s = state();
        std::lock_guard lock(s.mtx);
        if (s.failFindLine)
        {
            errno = ENOENT;
            return nullptr;
        }
    }
    // Lock released before calling getOrCreateLine (which also acquires mtx)
    return getOrCreateLine(name);
}

// --- Line info ---

unsigned int gpiod_line_offset(struct gpiod_line* line)
{
    return line ? line->offset : 0;
}

const char* gpiod_line_name(struct gpiod_line* line)
{
    return line ? line->name : "";
}

const char* gpiod_line_consumer(struct gpiod_line* line)
{
    return line ? line->consumer : "";
}

int gpiod_line_direction(struct gpiod_line* line)
{
    return line ? line->direction : 1;
}

int gpiod_line_active_state(struct gpiod_line* line)
{
    return line ? line->activeState : 1;
}

int gpiod_line_bias(struct gpiod_line* /*line*/)
{
    return 1; // BIAS_AS_IS
}

bool gpiod_line_is_used(struct gpiod_line* /*line*/)
{
    return false;
}

bool gpiod_line_is_open_drain(struct gpiod_line* /*line*/)
{
    return false;
}

bool gpiod_line_is_open_source(struct gpiod_line* /*line*/)
{
    return false;
}

bool gpiod_line_is_requested(struct gpiod_line* line)
{
    return line ? line->isRequested : false;
}

int gpiod_line_update(struct gpiod_line* /*line*/)
{
    return 0;
}

// --- Line request/release ---

// Override single-line request functions directly to prevent real libgpiod
// from accessing internal struct fields. The C++ wrapper calls these, which
// in the real library would build a bulk and call gpiod_line_request_bulk.
// By overriding here, we avoid any struct-layout mismatches.

int gpiod_line_request(struct gpiod_line* line,
                       const struct gpiod_line_request_config* config,
                       int /*default_val*/)
{
    if (!line)
    {
        return -1;
    }
    {
        auto& s = state();
        std::lock_guard lock(s.mtx);
        if (s.failLineRequest)
        {
            errno = EBUSY;
            return -1;
        }
    }
    line->isRequested = true;
    if (config && config->consumer)
    {
        strncpy(line->consumer, config->consumer, sizeof(line->consumer) - 1);
        line->consumer[sizeof(line->consumer) - 1] = '\0';
    }
    {
        auto& s = state();
        std::lock_guard lock(s.mtx);
        auto it = s.lines.find(line->name);
        if (it != s.lines.end())
        {
            it->second.requested = true;
        }
    }
    return 0;
}

int gpiod_line_request_input(struct gpiod_line* line, const char* consumer)
{
    struct gpiod_line_request_config config = {consumer, 1, 0};
    return gpiod_line_request(line, &config, 0);
}

int gpiod_line_request_output(struct gpiod_line* line, const char* consumer,
                              int default_val)
{
    struct gpiod_line_request_config config = {consumer, 2, 0};
    return gpiod_line_request(line, &config, default_val);
}

int gpiod_line_request_input_flags(struct gpiod_line* line,
                                   const char* consumer, int flags)
{
    struct gpiod_line_request_config config = {consumer, 1, flags};
    return gpiod_line_request(line, &config, 0);
}

int gpiod_line_request_output_flags(
    struct gpiod_line* line, const char* consumer, int flags, int default_val)
{
    struct gpiod_line_request_config config = {consumer, 2, flags};
    return gpiod_line_request(line, &config, default_val);
}

int gpiod_line_request_rising_edge_events(struct gpiod_line* line,
                                          const char* consumer)
{
    struct gpiod_line_request_config config = {consumer, 3, 0};
    return gpiod_line_request(line, &config, 0);
}

int gpiod_line_request_falling_edge_events(struct gpiod_line* line,
                                           const char* consumer)
{
    struct gpiod_line_request_config config = {consumer, 4, 0};
    return gpiod_line_request(line, &config, 0);
}

int gpiod_line_request_both_edges_events(struct gpiod_line* line,
                                         const char* consumer)
{
    struct gpiod_line_request_config config = {consumer, 5, 0};
    return gpiod_line_request(line, &config, 0);
}

int gpiod_line_request_rising_edge_events_flags(struct gpiod_line* line,
                                                const char* consumer, int flags)
{
    struct gpiod_line_request_config config = {consumer, 3, flags};
    return gpiod_line_request(line, &config, 0);
}

int gpiod_line_request_falling_edge_events_flags(
    struct gpiod_line* line, const char* consumer, int flags)
{
    struct gpiod_line_request_config config = {consumer, 4, flags};
    return gpiod_line_request(line, &config, 0);
}

int gpiod_line_request_both_edges_events_flags(struct gpiod_line* line,
                                               const char* consumer, int flags)
{
    struct gpiod_line_request_config config = {consumer, 5, flags};
    return gpiod_line_request(line, &config, 0);
}

void gpiod_line_release(struct gpiod_line* line)
{
    if (line)
    {
        line->isRequested = false;
    }
}

int gpiod_line_get_value(struct gpiod_line* line)
{
    if (!line)
    {
        return -1;
    }
    auto& s = state();
    std::lock_guard lock(s.mtx);
    auto it = s.lines.find(line->name);
    if (it != s.lines.end())
    {
        return it->second.value;
    }
    return line->value;
}

int gpiod_line_set_value(struct gpiod_line* line, int value)
{
    if (!line)
    {
        return -1;
    }
    line->value = value;
    return 0;
}

int gpiod_line_request_bulk(struct gpiod_line_bulk* bulk,
                            const struct gpiod_line_request_config* config,
                            const int* /*default_vals*/)
{
    if (!bulk || bulk->num_lines == 0)
    {
        return -1;
    }
    {
        auto& s = state();
        std::lock_guard lock(s.mtx);
        if (s.failLineRequest)
        {
            return -1;
        }
    }
    for (unsigned int i = 0; i < bulk->num_lines; i++)
    {
        struct gpiod_line* line = bulk->lines[i];
        if (line)
        {
            line->isRequested = true;
            if (config && config->consumer)
            {
                strncpy(line->consumer, config->consumer,
                        sizeof(line->consumer) - 1);
                line->consumer[sizeof(line->consumer) - 1] = '\0';
            }

            // Update mock state
            auto& s = state();
            std::lock_guard lock(s.mtx);
            auto it = s.lines.find(line->name);
            if (it != s.lines.end())
            {
                it->second.requested = true;
            }
        }
    }
    return 0;
}

void gpiod_line_release_bulk(struct gpiod_line_bulk* bulk)
{
    if (!bulk)
    {
        return;
    }
    for (unsigned int i = 0; i < bulk->num_lines; i++)
    {
        if (bulk->lines[i])
        {
            bulk->lines[i]->isRequested = false;
        }
    }
}

// --- Line value ---

int gpiod_line_get_value_bulk(struct gpiod_line_bulk* bulk, int* values)
{
    if (!bulk || !values)
    {
        return -1;
    }
    for (unsigned int i = 0; i < bulk->num_lines; i++)
    {
        struct gpiod_line* line = bulk->lines[i];
        if (line)
        {
            // Check MockGPIO state first
            auto& s = state();
            std::lock_guard lock(s.mtx);
            auto it = s.lines.find(line->name);
            if (it != s.lines.end())
            {
                values[i] = it->second.value;
            }
            else
            {
                values[i] = line->value;
            }
        }
        else
        {
            values[i] = 0;
        }
    }
    return 0;
}

int gpiod_line_set_value_bulk(struct gpiod_line_bulk* bulk, const int* values)
{
    if (!bulk || !values)
    {
        return -1;
    }
    for (unsigned int i = 0; i < bulk->num_lines; i++)
    {
        if (bulk->lines[i])
        {
            bulk->lines[i]->value = values[i];
        }
    }
    return 0;
}

int gpiod_line_set_config_bulk(struct gpiod_line_bulk* /*bulk*/,
                               int /*direction*/, int /*flags*/,
                               const int* /*values*/)
{
    return 0;
}

int gpiod_line_set_flags_bulk(struct gpiod_line_bulk* /*bulk*/, int /*flags*/)
{
    return 0;
}

int gpiod_line_set_direction_input_bulk(struct gpiod_line_bulk* /*bulk*/)
{
    return 0;
}

int gpiod_line_set_direction_output_bulk(struct gpiod_line_bulk* /*bulk*/,
                                         const int* /*values*/)
{
    return 0;
}

// --- Events ---

int gpiod_line_event_wait_bulk(struct gpiod_line_bulk* /*bulk*/,
                               const struct timespec* /*timeout*/,
                               struct gpiod_line_bulk* /*event_bulk*/)
{
    return 0; // No events
}

int gpiod_line_event_read(struct gpiod_line* /*line*/,
                          struct gpiod_line_event* event)
{
    if (!event)
    {
        return -1;
    }

    auto& s = state();
    std::lock_guard lock(s.mtx);
    if (s.failEventRead)
    {
        errno = EAGAIN;
        return -1;
    }
    event->ts.tv_sec = 0;
    event->ts.tv_nsec = 0;
    event->event_type = 1; // RISING_EDGE

    if (s.eventReadCallback)
    {
        s.eventReadCallback();
    }
    return 0;
}

int gpiod_line_event_read_multiple(struct gpiod_line* /*line*/,
                                   struct gpiod_line_event* events,
                                   unsigned int num_events)
{
    if (!events || num_events == 0)
    {
        return -1;
    }
    events[0].ts.tv_sec = 0;
    events[0].ts.tv_nsec = 0;
    events[0].event_type = 1;
    return 1; // 1 event read
}

int gpiod_line_event_get_fd(struct gpiod_line* /*line*/)
{
    auto& s = state();
    std::lock_guard lock(s.mtx);
    if (s.failEventGetFd)
    {
        return -1;
    }
    // Always create a fresh eventfd — the caller (stream_descriptor) owns it
    // and will close it on destruction
    return eventfd(0, EFD_NONBLOCK);
}

// --- Line iterator ---

struct gpiod_line_iter
{
    struct gpiod_chip* chip;
    unsigned int current;
};

struct gpiod_line_iter* gpiod_line_iter_new(struct gpiod_chip* chip)
{
    auto* iter = new gpiod_line_iter{};
    iter->chip = chip;
    iter->current = 0;
    return iter;
}

void gpiod_line_iter_free(struct gpiod_line_iter* iter)
{
    delete iter;
}

struct gpiod_line* gpiod_line_iter_next(struct gpiod_line_iter* iter)
{
    if (!iter || !iter->chip || iter->current >= iter->chip->numLines)
    {
        return nullptr;
    }
    return gpiod_chip_get_line(iter->chip, iter->current++);
}

} // extern "C"
