#include "rdmapp/batch_cq_poller.h"

#include <memory>
#include <stdexcept>
#include <chrono>
#include <coroutine>
#include <iostream>
#include <thread>
#include <infiniband/verbs.h>

#include "rdmapp/executor.h"

#include "rdmapp/detail/debug.h"

namespace rdmapp {

batch_cq_poller::batch_cq_poller(std::vector<std::shared_ptr<cq>>& cqs, bool is_recv, size_t batch_size)
    : batch_cq_poller(cqs, is_recv, std::make_shared<executor>(), batch_size) {}

batch_cq_poller::batch_cq_poller(std::vector<std::shared_ptr<cq>>& cqs, bool is_recv, std::shared_ptr<executor> executor,
                     size_t batch_size)
    : cqs_(cqs), stopped_(false), connected_(false), executor_(executor), wc_vec_(batch_size) {
  if (is_recv) {
    poller_thread_ = std::jthread(&batch_cq_poller::recv_worker, this);
  } else {
    poller_thread_ = std::jthread(&batch_cq_poller::send_worker, this);
  } 
}

batch_cq_poller::~batch_cq_poller() {
  stopped_ = true;
  poller_thread_.join();
}

void batch_cq_poller::connect_done() {
  connected_ = true;
}

void batch_cq_poller::connect_loop() {
  while (!connected_) {
    try {
      for (auto &cq_ : cqs_) {
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

void batch_cq_poller::recv_worker() {
  connect_loop();
  auto st = std::chrono::high_resolution_clock::now();
  auto st2 = std::chrono::high_resolution_clock::now();
  int tot = 0;
  while (!stopped_) {
    try {
      for (auto &cq_ : cqs_) {
        auto nr_wc = cq_->poll(wc_vec_);
        tot++;
        if (nr_wc != 0) {
          auto et = std::chrono::high_resolution_clock::now();
          auto elapsed1 = std::chrono::duration_cast<std::chrono::nanoseconds>(st2 - st);
          auto elapsed2 = std::chrono::duration_cast<std::chrono::nanoseconds>(et - st2);
          std::cout << nr_wc << " " << tot << " " << elapsed1.count() << " " << elapsed2.count() << "\n";
          tot = 0;
          st = et;
          for (size_t i = 0; i < nr_wc; ++i) {
            auto &wc = wc_vec_[i];
            executor_->process_wc(wc);
          }
          st2 = std::chrono::high_resolution_clock::now();
        }
      }
    } catch (...) {
      stopped_ = true;
      return;
    }
  }
}

void batch_cq_poller::send_worker() {
  connect_loop();
  while (!stopped_) {
    try {
      for (auto &cq_ : cqs_) {
        auto nr_wc = cq_->poll(wc_vec_);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    } catch (...) {
      stopped_ = true;
      return;
    }
  }
}

} // namespace rdmapp