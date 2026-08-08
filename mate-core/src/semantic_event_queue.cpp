#include "cec_injector/mate/semantic_event_queue.h"

#include <algorithm>
#include <stdexcept>

namespace cec_injector::mate {

SemanticEventQueue::SemanticEventQueue(std::size_t capacity)
    : capacity_(capacity) {
  if (capacity_ == 0) throw std::invalid_argument("event capacity is zero");
}

EventSubmitResult SemanticEventQueue::submit(LifecycleEvent event) {
  if (active_ == event ||
      std::find(queue_.begin(), queue_.end(), event) != queue_.end()) {
    return EventSubmitResult::Duplicate;
  }
  if (queue_.size() >= capacity_) return EventSubmitResult::Full;
  queue_.push_back(event);
  return EventSubmitResult::Accepted;
}

std::optional<LifecycleEvent> SemanticEventQueue::begin_next() {
  if (active_ || queue_.empty()) return std::nullopt;
  active_ = queue_.front();
  queue_.pop_front();
  return active_;
}

void SemanticEventQueue::complete_active() { active_.reset(); }

void SemanticEventQueue::clear() {
  queue_.clear();
  active_.reset();
}

}  // namespace cec_injector::mate
