#pragma once

#include <cstddef>
#include <deque>
#include <optional>

#include "cec_injector/mate/policy.h"

namespace cec_injector::mate {

enum class EventSubmitResult { Accepted, Duplicate, Full };

class SemanticEventQueue {
 public:
  explicit SemanticEventQueue(std::size_t capacity);

  [[nodiscard]] EventSubmitResult submit(LifecycleEvent event);
  [[nodiscard]] std::optional<LifecycleEvent> begin_next();
  void complete_active();
  void clear();

  [[nodiscard]] std::size_t pending() const noexcept { return queue_.size(); }
  [[nodiscard]] std::optional<LifecycleEvent> active() const noexcept {
    return active_;
  }

 private:
  std::size_t capacity_;
  std::deque<LifecycleEvent> queue_;
  std::optional<LifecycleEvent> active_;
};

}  // namespace cec_injector::mate
