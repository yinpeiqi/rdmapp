#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <infiniband/verbs.h>

#include "rdmapp/cq.h"
// #include "rdmapp/detail/concurrent_queue.h"

namespace rdmapp {

/**
 * @brief This class is used to poll a completion queue.
 *
 */
class poll_executor {
  using work_queue = detail::ConcurrentQueue<void*>;
  std::shared_ptr<work_queue> work_queue_;
  std::atomic<bool> listening_work_queue_;
  std::vector<std::shared_ptr<cq>> send_cqs_;
  std::vector<std::shared_ptr<cq>> recv_cqs_;
  std::atomic<bool> connected_;
  std::atomic<bool> stopped_;
  std::jthread worker_thread_;
  std::vector<struct ibv_wc> wc_vec_;
  void work();

public:
  class closed_exception : public std::runtime_error {
  public:
    closed_exception() : std::runtime_error("Queue is closed") {}
  };

  void connect_loop();
  void connect_done();
  void enable_listening_work_queue();
  void disable_listening_work_queue();
  void work_enqueue(void* h_ptr);
  /**
   * @brief Construct a new cq poller object. A new executor will be created.
   *
   * @param cq The completion queue to poll.
   * @param batch_size The number of completion entries to poll at a time.
   */
  poll_executor(std::vector<std::shared_ptr<cq>>& send_cqs, 
                std::vector<std::shared_ptr<cq>>& recv_cqs,
                size_t batch_size = 16);

  ~poll_executor();
};

} // namespace rdmapp