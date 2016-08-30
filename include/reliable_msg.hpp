#pragma once

#include <array>

#include <caf/all.hpp>
#include <caf/io/all.hpp>

namespace relm {

using ack_atom        = caf::atom_constant<caf::atom("ack")>;
using retransmit_atom = caf::atom_constant<caf::atom("retransmit")>;

struct reliable_msg {
  template <class Inspector>
  friend typename Inspector::result_type inspect(Inspector& f, reliable_msg& x);
  friend std::string to_string(const reliable_msg& msg);

  static reliable_msg ack(int32_t seq);
  static reliable_msg ack(int32_t seq, int32_t num_nacks,
                          std::array<int32_t, 3> nacks);
  static reliable_msg msg(caf::atom_value atm, int32_t content, int32_t seq);

  reliable_msg();
  reliable_msg(caf::atom_value atm, int32_t content, int32_t seq,
               int32_t num_acks, std::array<int32_t, 3> nacks);

  bool operator<(const reliable_msg& other);

  caf::atom_value atm;
  int32_t content;
  int32_t seq;
  int32_t num_nacks;
  std::array<int32_t, 3> nacks;
};

bool operator<(const reliable_msg& a, const reliable_msg& b);

std::string to_string(const reliable_msg& msg);

template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, reliable_msg& x) {
  return f(caf::meta::type_name("reliable_msg"), x.atm, x.seq, x.content);
}

} // namespace relm
