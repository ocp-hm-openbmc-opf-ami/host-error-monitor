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
// Mock gpiod C library for unit testing
//
// Strategy:
//   The production code links against libgpiodcxx.so (C++ bindings)
//   which in turn calls libgpiod.so C functions through PLT.
//   By providing C-linkage definitions of these functions in the
//   test executable, ELF symbol resolution gives our definitions
//   priority over the ones in libgpiod.so.
//
//   This allows all GPIO monitor constructors to succeed (valid=true)
//   so we can test their full logic paths including assertHandler,
//   deassertHandler, logEvent, startMonitoring, etc.
//
// Control:
//   Tests can control mock behavior through the MockGPIO namespace.
//   - MockGPIO::setLineValue(name, value) - set GPIO line value
//   - MockGPIO::getRegisteredLines() - get all registered lines
//   - MockGPIO::reset() - reset all mock state
//   - MockGPIO::setEventFd(fd) - set the fd returned by event_get_fd
// ============================================================
#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace MockGPIO
{

/// Set the value returned by get_value for a given line name
void setLineValue(const std::string& name, int value);

/// Get the current mock value for a line
int getLineValue(const std::string& name);

/// Get the event fd to use for async_wait
int getEventFd();

/// Set the event fd (caller creates e.g. an eventfd or pipe)
void setEventFd(int fd);

/// Reset all mock state
void reset();

/// Check if a line name was requested (find_line was called for it)
bool wasLineRequested(const std::string& name);

/// Get all line names that have been created
std::vector<std::string> getRegisteredLines();

/// Set a callback to be invoked when event_read is called
void setEventReadCallback(std::function<void()> cb);

/// Make gpiod_line_event_read return failure (-1) immediately.
/// Useful for flushEvents() which loops until event_read throws.
void setFailEventRead(bool fail);

/// Make gpiod_chip_find_line return NULL (line not found).
void setFailFindLine(bool fail);

/// Make gpiod_line_request_bulk return -1 (request failure).
void setFailLineRequest(bool fail);

/// Make gpiod_line_event_get_fd return -1 (fd failure).
void setFailEventGetFd(bool fail);

} // namespace MockGPIO
