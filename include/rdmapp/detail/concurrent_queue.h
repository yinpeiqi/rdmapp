#include <atomic>
#include <vector>
#include <memory>
#include <stdexcept>

namespace rdmapp {

namespace detail {

template<class T>
class ConcurrentQueue {
 public:
  explicit ConcurrentQueue(size_t capacity) 
      : capacity_(capacity + 1), is_closed_(false) {
      if (capacity < 1) {
          throw std::invalid_argument("Capacity must be at least 1");
      }
      buffer_.resize(capacity_);
      head_.store(0);
      tail_.store(0);
  }

  ConcurrentQueue(const ConcurrentQueue&) = delete;
  ConcurrentQueue& operator=(const ConcurrentQueue&) = delete;

  void reset() noexcept {
    head_.store(0);
    tail_.store(0);
    is_closed_.store(false);
  }

  void close() noexcept {
    is_closed_.store(true, std::memory_order_release);
  }

  bool is_closed() const noexcept {
    return is_closed_.load(std::memory_order_acquire);
  }

  bool push(const T& value) {
    const int current_tail = tail_.load(std::memory_order_relaxed);
    const int next_tail = increment(current_tail);

    if (next_tail == head_.load(std::memory_order_acquire) || is_closed()) [[unlikely]] {
      return false;
    }

    buffer_[current_tail] = value;
    tail_.store(next_tail, std::memory_order_release);
    return true;
  }

  bool pop(T& value) {
    int current_head;
    int next_head;
    do {
      current_head = head_.load(std::memory_order_relaxed);
      if (current_head == tail_.load(std::memory_order_acquire) || is_closed()) {
          return false;
      }

      value = buffer_[current_head];
      next_head = increment(current_head);
    } while (!head_.compare_exchange_weak(
      current_head, next_head,
      std::memory_order_release,
      std::memory_order_relaxed
    ));

    return true;
  }

  bool empty() const noexcept {
    return head_.load(std::memory_order_relaxed) == 
           tail_.load(std::memory_order_relaxed);
  }

  size_t size() const noexcept {
    const int head = head_.load(std::memory_order_relaxed);
    const int tail = tail_.load(std::memory_order_relaxed);
    return (tail >= head) ? (tail - head) : (capacity_ + tail - head);
  }

 private:
  inline int increment(int index) const noexcept {
    return (index + 1) % capacity_;
  }

  const int capacity_;
  std::vector<T> buffer_;

  alignas(64) std::atomic<int> head_;
  alignas(64) std::atomic<int> tail_;
  alignas(64) std::atomic<bool> is_closed_;
};

}
}