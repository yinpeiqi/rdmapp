#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

#include <infiniband/verbs.h>

#include "rdmapp/cq.h"
#include "rdmapp/executor.h"

namespace rdmapp {

/**
 * @brief This class is used to poll a completion queue.
 *
 */
class batch_cq_poller {
  std::vector<std::shared_ptr<cq>> cqs_;
  std::atomic<bool> connected_;
  std::atomic<bool> stopped_;
  std::jthread poller_thread_;
  std::shared_ptr<executor> executor_;
  std::vector<struct ibv_wc> wc_vec_;
  void recv_worker();
  void send_worker();

public:
  void connect_loop();
  void connect_done();
  /**
   * @brief Construct a new cq poller object. A new executor will be created.
   *
   * @param cq The completion queue to poll.
   * @param batch_size The number of completion entries to poll at a time.
   */
  batch_cq_poller(std::vector<std::shared_ptr<cq>>& cqs, bool is_recv = true, size_t batch_size = 16);

  /**
   * @brief Construct a new cq poller object.
   *
   * @param cq The completion queue to poll.
   * @param executor The executor to use to process the completion entries.
   * @param batch_size The number of completion entries to poll at a time.
   */
  batch_cq_poller(std::vector<std::shared_ptr<cq>>& cqs, bool is_recv, std::shared_ptr<executor> executor,
            size_t batch_size = 16);

  ~batch_cq_poller();
};

} // namespace rdmapp