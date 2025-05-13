#pragma once

#include <functional>
#include <thread>
#include <vector>

#include <infiniband/verbs.h>

#include "rdmapp/detail/blocking_queue.h"
#include "rdmapp/detail/concurrent_queue.h"

namespace rdmapp {

/**
 * @brief This class is used to execute callbacks of completion entries.
 *
 */
class executor {
  using work_queue = detail::ConcurrentQueue<void*>;
  std::vector<std::jthread> workers_;
  std::shared_ptr<work_queue> work_queue_;
  void worker_fn(size_t worker_id);

public:
  class closed_exception : public std::runtime_error {
  public:
    closed_exception() : std::runtime_error("Queue is closed") {}
  };
  using callback_fn = std::function<void(void* h_ptr)>;
  using callback_ptr = callback_fn *;

  /**
   * @brief Construct a new executor object
   *
   * @param nr_worker The number of worker threads to use.
   */
  executor(size_t nr_worker = 4);

  /**
   * @brief Process a completion entry.
   *
   * @param wc The completion entry to process.
   */
  void process_wc(void* h_ptr);

  /**
   * @brief Shutdown the executor.
   *
   */
  void shutdown();

  ~executor();

  /**
   * @brief Make a callback function that will be called when a completion entry
   * is processed. The callback function will be called in the executor's
   * thread. The lifetime of this pointer is controlled by the executor.
   * @tparam T The type of the callback function.
   * @param cb The callback function.
   * @return callback_ptr The callback function pointer.
   */
  template <class T> static callback_ptr make_callback(T const &cb) {
    return new executor::callback_fn(cb);
  }

  /**
   * @brief Destroy a callback function.
   *
   * @param cb The callback function pointer.
   */
  static void destroy_callback(callback_ptr cb);
};

} // namespace rdmapp