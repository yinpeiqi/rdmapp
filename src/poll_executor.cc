#include "rdmapp/batch_cq_poller.h"

#include <memory>
#include <stdexcept>
#include <chrono>
#include <coroutine>
#include <iostream>
#include <thread>
#include <infiniband/verbs.h>

#include "rdmapp/poll_executor.h"

#include "rdmapp/detail/debug.h"

namespace rdmapp {

poll_executor::poll_executor(std::vector<std::shared_ptr<cq>>& send_cqs,
                             std::vector<std::shared_ptr<cq>>& recv_cqs,
                             size_t batch_size)
    : send_cqs_(send_cqs), recv_cqs_(recv_cqs), stopped_(false), connected_(false), wc_vec_(batch_size) {
  work_queue_ = std::make_shared<work_queue>(4096);
  listening_work_queue_ = false;
  worker_thread_ = std::jthread(&poll_executor::work, this);
}

poll_executor::~poll_executor() {
  stopped_ = true;
  worker_thread_.join();
}

void poll_executor::connect_done() {
  connected_ = true;
}

void poll_executor::enable_listening_work_queue() {
  listening_work_queue_.store(true, std::memory_order_relaxed);
}

void poll_executor::disable_listening_work_queue() {
  listening_work_queue_.store(false, std::memory_order_relaxed);
}

void poll_executor::work_enqueue(void* h_ptr) {
  work_queue_->push(h_ptr);
}

void poll_executor::connect_loop() {
  while (!connected_) {
    try {
      for (auto &cq_ : send_cqs_) {
        auto nr_wc = cq_->poll(wc_vec_);
        if (nr_wc != 0) {
          for (size_t i = 0; i < nr_wc; ++i) {
            auto &wc = wc_vec_[i];
            struct ibv_wc *wc_ptr = reinterpret_cast<struct ibv_wc *>(wc.wr_id);
            *wc_ptr = wc;
            std::coroutine_handle<> h = std::coroutine_handle<>::from_address(
                *reinterpret_cast<void **>(wc_ptr + 1));
            h.resume();
          }
        }
      }
      for (auto &cq_ : recv_cqs_) {
        auto nr_wc = cq_->poll(wc_vec_);
        if (nr_wc != 0) {
          for (size_t i = 0; i < nr_wc; ++i) {
            auto &wc = wc_vec_[i];
            struct ibv_wc *wc_ptr = reinterpret_cast<struct ibv_wc *>(wc.wr_id);
            *wc_ptr = wc;
            std::coroutine_handle<> h = std::coroutine_handle<>::from_address(
                *reinterpret_cast<void **>(wc_ptr + 1));
            h.resume();
          }
        }
      }
    } catch (...) {
      stopped_ = true;
      return;
    }
  }
}

void poll_executor::work() {
  connect_loop();
  while (!stopped_) {
    try {
      for (auto &cq_ : recv_cqs_) {
        auto nr_wc = cq_->poll(wc_vec_);
        if (nr_wc != 0) {
          for (size_t i = 0; i < nr_wc; ++i) {
            auto &wc = wc_vec_[i];
            struct ibv_wc *wc_ptr = reinterpret_cast<struct ibv_wc *>(wc.wr_id);
            *wc_ptr = wc;
            std::coroutine_handle<> h = std::coroutine_handle<>::from_address(
                *reinterpret_cast<void **>(wc_ptr + 1));
            h.resume();
          }
        }
      }
      for (auto &cq_ : send_cqs_) {
        auto nr_wc = cq_->poll(wc_vec_);
      }
      // process the work queue from the other threads
      if (listening_work_queue_.load(std::memory_order_relaxed)) {
        void* h_ptr;
        if (!work_queue_->empty()) {
          while (work_queue_->pop(h_ptr)) {
            std::coroutine_handle<> h = std::coroutine_handle<>::from_address(h_ptr);
            h.resume();
          }
        }
      }
    } catch (...) {
      std::cout << "poll_executor stopped" << std::endl;
      stopped_ = true;
      return;
    }
  }
}

} // namespace rdmapp