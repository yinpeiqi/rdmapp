#include <cassert>
#include "rdmapp/qp.h"

namespace rdmapp {

qp::light_send_awaitable::light_send_awaitable(qp* qp,
                                  size_t length,
                                  local_mr* local_mr,
                                  remote_mr* remote_mr, uint32_t imm)
    : qp_(qp), length_(length), local_mr_(local_mr), remote_mr_(remote_mr), imm_(imm) {}

qp::light_send_awaitable qp::write_with_imm(remote_mr* remote_mr,
	local_mr* local_mr, size_t length, uint32_t imm) {
	return qp::light_send_awaitable(this, length, local_mr, remote_mr, imm);
}


bool qp::light_send_awaitable::await_ready() const noexcept { return false; }
bool qp::light_send_awaitable::await_suspend(std::coroutine_handle<> h) noexcept {
	coroutine_addr_ = h.address();

	if (length_ == -1) {
		length_ = local_mr_->length();
	}
	// fill_local_sge
  struct ibv_sge send_sge = {};
  send_sge.addr = reinterpret_cast<uint64_t>(local_mr_->addr());
  send_sge.length = length_;
  send_sge.lkey = local_mr_->lkey();

	struct ibv_send_wr send_wr = {};
	struct ibv_send_wr *bad_send_wr = nullptr;
	send_wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
	send_wr.next = nullptr;
	send_wr.num_sge = 1;
	send_wr.wr_id = reinterpret_cast<uint64_t>(this);
	send_wr.send_flags = IBV_SEND_SIGNALED;
	send_wr.sg_list = &send_sge;
	assert(remote_mr_->addr() != nullptr);
	send_wr.wr.rdma.remote_addr = reinterpret_cast<uint64_t>(remote_mr_->addr());
	send_wr.wr.rdma.rkey = remote_mr_->rkey();
	send_wr.imm_data = imm_;

	qp_->post_send(send_wr, bad_send_wr);
	return true;
}

uint32_t qp::light_send_awaitable::await_resume() const {
	return wc_.byte_len;
}


qp::light_recv_awaitable qp::recv(local_mr* local_mr) {
  return qp::light_recv_awaitable(this, local_mr);
}

qp::light_recv_awaitable::light_recv_awaitable(qp* qp, local_mr* local_mr)
                          : qp_(qp), local_mr_(local_mr), wc_() {}

bool qp::light_recv_awaitable::await_ready() const noexcept { return false; }
bool qp::light_recv_awaitable::await_suspend(std::coroutine_handle<> h) noexcept {
  coroutine_addr_ = h.address();
	// fill_local_sge
	struct ibv_sge recv_sge = {};
  recv_sge.addr = reinterpret_cast<uint64_t>(local_mr_->addr());
  recv_sge.length = local_mr_->length();
  recv_sge.lkey = local_mr_->lkey();

  struct ibv_recv_wr recv_wr = {};
  struct ibv_recv_wr *bad_recv_wr = nullptr;
  recv_wr.next = nullptr;
  recv_wr.num_sge = 1;
  recv_wr.wr_id = reinterpret_cast<uint64_t>(this);
  recv_wr.sg_list = &recv_sge;

  qp_->post_recv(recv_wr, bad_recv_wr);
  return true;
}

std::pair<uint32_t, std::optional<uint32_t>>
qp::light_recv_awaitable::await_resume() const {
  if (wc_.wc_flags & IBV_WC_WITH_IMM) {
    return std::make_pair(wc_.byte_len, wc_.imm_data);
  }
  return std::make_pair(wc_.byte_len, std::nullopt);
}

}