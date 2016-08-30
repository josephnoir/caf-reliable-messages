
#include <sstream>

#include "include/reliable_msg.hpp"

using namespace std;
using namespace caf;

namespace relm {

reliable_msg reliable_msg::ack(int32_t seq) {
  return reliable_msg{ack_atom::value, 0, seq, 0, {{0,0,0}}};
}

reliable_msg reliable_msg::ack(int32_t seq, int32_t num_nacks,
                               std::array<int32_t, 3> nacks) {
  return reliable_msg{ack_atom::value, 0, seq, num_nacks, std::move(nacks)};
}

reliable_msg reliable_msg::msg(atom_value atm, int32_t content, int32_t seq) {
  return reliable_msg{atm, content, seq, 0, {{0,0,0}}};
}

reliable_msg::reliable_msg()
  : content{0},
    seq{0},
    num_nacks{0},
    nacks{{0,0,0}} {
  // nop
}

reliable_msg::reliable_msg(caf::atom_value atm, int32_t content, int32_t seq,
                           int32_t num_nacks, std::array<int32_t, 3> nacks)
    : atm{atm},
      content{content},
      seq{seq},
      num_nacks{num_nacks},
      nacks{nacks} {
 // nop
}

bool reliable_msg::operator<(const reliable_msg& other) {
  return this->seq < other.seq;
}

bool operator<(const reliable_msg& a, const reliable_msg& b) {
  return a.seq < b.seq;
}

string to_string(const reliable_msg& msg) {
  stringstream strm;
  strm << "{" << to_string(msg.atm) << ", " << std::to_string(msg.content)
       << ", " << std::to_string(msg.seq) << ", "
       << std::to_string(msg.num_nacks);
  if (msg.num_nacks > 0) {
    strm << ", [";
    for (int32_t i = 0; i < msg.num_nacks; ++i) {
      strm << msg.nacks[i];
      if (i + 1 < msg.num_nacks) {
        strm << ", ";
      }
    }
    strm << "]";
  }
  strm << "}";
  return strm.str();
}

} // namespace relm
