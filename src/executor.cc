#include "rdmapp/executor.h"
#include <chrono>
#include <coroutine>
#include <iostream>

#include "rdmapp/detail/blocking_queue.h"
#include "rdmapp/detail/debug.h"

namespace rdmapp {

executor::executor(size_t nr_worker) {
  work_queue_ = std::make_shared<work_queue>(4096);
  for (size_t i = 0; i < nr_worker; ++i) {
    workers_.emplace_back(&executor::worker_fn, this, i);
    std::cout << "executor worker " << i << " started\n";
  }
}

void executor::worker_fn(size_t worker_id) {
  struct ibv_wc wc;
  try {
    while (true) {
      while (!work_queue_->pop(wc)) {
        if (work_queue_->is_closed()) [[unlikely]] {
          throw closed_exception();
        }
      }
      struct ibv_wc *wc_ptr = reinterpret_cast<struct ibv_wc *>(wc.wr_id);
      *wc_ptr = wc;
      std::coroutine_handle<> h = std::coroutine_handle<>::from_address(
          *reinterpret_cast<void **>(wc_ptr + 1));
      // auto st = std::chrono::high_resolution_clock::now();
      h.resume();
      // auto et = std::chrono::high_resolution_clock::now();
      // auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(et - st);
      // std::cout << "process_wc " << elapsed.count() << " ns\n";
    }
  } catch (...) {
    RDMAPP_LOG_TRACE("executor worker %lu exited", worker_id);
  }
}

void executor::process_wc(struct ibv_wc const &wc) {
  while (!work_queue_->push(wc)) {
    if (work_queue_->is_closed()) [[unlikely]] {
      throw closed_exception();
    }
  }
  // struct ibv_wc *wc_ptr = reinterpret_cast<struct ibv_wc *>(wc.wr_id);
  // *wc_ptr = wc;
  // std::coroutine_handle<> h = std::coroutine_handle<>::from_address(
  //     *reinterpret_cast<void **>(wc_ptr + 1));
  // // auto st = std::chrono::high_resolution_clock::now();
  // h.resume();
  // auto et = std::chrono::high_resolution_clock::now();
  // auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(et - st);
  // std::cout << "process_wc " << elapsed.count() << " ns\n";
}

void executor::shutdown() { work_queue_->close(); }

void executor::destroy_callback(callback_ptr cb) { delete cb; }

executor::~executor() {
  shutdown();
  for (auto &&worker : workers_) {
    worker.join();
  }
}

} // namespace rdmapp