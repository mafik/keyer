#include "main_loop.hpp"

#include "freertos/portmacro.h"
#include "freertos/projdefs.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

namespace atmt {

namespace {

struct TaskWrapper {
  void (*func)(uint64_t);
  uint64_t data;

  // Trampoline function for std::function
  static void StdFunctionTrampoline(uint64_t data) {
    auto *f = static_cast<function<void()> *>((void *)data);
    (*f)();
    delete f;
  }

  // For raw function pointer (void (*)(void*)) + void* arg
  static TaskWrapper FromRaw(void (*fn)(uint64_t), uint64_t arg) {
    return TaskWrapper{fn, arg};
  }

  // For std::function - uses a trampoline
  static TaskWrapper FromStdFunction(function<void()> *fn) {
    return TaskWrapper{&StdFunctionTrampoline, (uint64_t)fn};
  }

  void Execute() const { func(data); }
};

} // namespace

// Queue for passing events from other tasks/cores to the main loop
QueueHandle_t event_queue = nullptr;

void InitMainLoop() { event_queue = xQueueCreate(100, sizeof(TaskWrapper)); }

void RunOnMain(function<void()> event) {
  if (xPortGetCoreID() == 1) {
    event();
  } else {
    // Allocate event on heap to pass to main loop
    auto *heap_event = new function<void()>(std::move(event));
    TaskWrapper wrapper = TaskWrapper::FromStdFunction(heap_event);

    if (xQueueSendToBack(event_queue, &wrapper, portMAX_DELAY) != pdTRUE) {
      // Failed to queue, clean up
      delete heap_event;
    }
  }
}

void RunOnMain(void (*func)(uint64_t), uint64_t arg) {
  if (xPortGetCoreID() == 1) {
    func(arg);
  } else {
    TaskWrapper wrapper = TaskWrapper::FromRaw(func, arg);
    xQueueSendToBack(event_queue, &wrapper, portMAX_DELAY);
  }
}

void IRAM_ATTR RunOnMainISR(void (*func)(uint64_t), uint64_t arg) {
  TaskWrapper wrapper = TaskWrapper::FromRaw(func, arg);
  xQueueSendToBackFromISR(event_queue, &wrapper, 0);
}

bool MainLoopNonBlocking() {
  TaskWrapper wrapper;
  if (xQueueReceive(event_queue, &wrapper, 0) == pdTRUE) {
    wrapper.Execute();
    return true;
  }
  return false;
}

bool MainLoopBlocking(optional<time_point> deadline) {
  TickType_t timeout;

  if (deadline.has_value()) {
    auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          *deadline - clock::now())
                          .count();
    if (timeout_ms <= 0) {
      return false;
    }
    timeout = pdMS_TO_TICKS(timeout_ms);
  } else {
    timeout = portMAX_DELAY;
  }
  TaskWrapper wrapper;
  if (xQueueReceive(event_queue, &wrapper, timeout) == pdTRUE) {
    wrapper.Execute();
    return true;
  }

  return false;
}

} // namespace atmt
