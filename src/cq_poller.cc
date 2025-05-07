#include "rdmapp/cq_poller.h"

#include <memory>
#include <stdexcept>
#include <chrono>
#include <iostream>
#include <thread>
#include <infiniband/verbs.h>

#include "rdmapp/executor.h"

#include "rdmapp/detail/debug.h"

namespace rdmapp {

cq_poller::cq_poller(std::shared_ptr<cq> cq, bool is_recv, size_t batch_size)
    : cq_poller(cq, is_recv, std::make_shared<executor>(), batch_size) {}

cq_poller::cq_poller(std::shared_ptr<cq> cq, bool is_recv, std::shared_ptr<executor> executor,
                     size_t batch_size)
    : cq_(cq), stopped_(false), executor_(executor), wc_vec_(batch_size) {
  if (is_recv) {
    poller_thread_ = std::jthread(&cq_poller::recv_worker, this);
  } else {
    poller_thread_ = std::jthread(&cq_poller::send_worker, this);
  } 
}

cq_poller::~cq_poller() {
  stopped_ = true;
  poller_thread_.join();
}

void cq_poller::recv_worker() {
  auto st = std::chrono::high_resolution_clock::now();
  auto st2 = std::chrono::high_resolution_clock::now();
  int tot = 0;
  while (!stopped_) {
    try {
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
          // RDMAPP_LOG_TRACE("polled cqe wr_id=%p status=%d", reinterpret_cast<void *>(wc.wr_id), wc.status);
          executor_->process_wc(wc);
        }
        st2 = std::chrono::high_resolution_clock::now();
      }
    } catch (...) {
      std::cout << "recv cq_poller stopped" << std::endl;
      stopped_ = true;
      return;
    }
  }
}

void cq_poller::send_worker() {
  while (!stopped_) {
    try {
      auto nr_wc = cq_->poll(wc_vec_);
      if (nr_wc != 0) {
        for (size_t i = 0; i < nr_wc; ++i) {
          auto &wc = wc_vec_[i];
          if (wc.opcode == IBV_WR_RDMA_WRITE_WITH_IMM) {
            // when send done, all op is write_with_imm, this thread can keep sleeping
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
          }
          // only process "send"
          executor_->process_wc(wc);
        }
      }
    } catch (...) {
      std::cout << "send cq_poller stopped" << std::endl;
      stopped_ = true;
      return;
    }
  }
}

} // namespace rdmapp