#pragma once

#include "common.hpp"
#include "esp_attr.h"

namespace atmt {

void InitMainLoop();

void RunOnMain(function<void()> event);
void RunOnMain(void (*func)(uint64_t), uint64_t arg);

void IRAM_ATTR RunOnMainISR(void (*func)(uint64_t), uint64_t arg);

bool MainLoopNonBlocking();

bool MainLoopBlocking(optional<time_point> deadline = std::nullopt);

} // namespace atmt
