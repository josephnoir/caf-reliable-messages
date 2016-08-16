// simple_broker example from CAF as a base

#ifdef WIN32
# define _WIN32_WINNT 0x0600
# include <Winsock2.h>
#else
# include <arpa/inet.h> // htonl
#endif

#include <chrono>
#include <vector>
#include <iostream>

#include <caf/all.hpp>
#include <caf/io/all.hpp>

namespace relm {

using seq_num_t = int32_t;
using retransmit_cnt = int32_t;
using clk = std::chrono::high_resolution_clock;
using tp = clk::time_point;

using ping_atom = caf::atom_constant<caf::atom("ping")>;
using pong_atom = caf::atom_constant<caf::atom("pong")>;
using kickoff_atom = caf::atom_constant<caf::atom("kickoff")>;

struct pending_msg {
  caf::atom_value atm;
  seq_num_t seq;
  int32_t content;
};

template <class Inspector>
typename Inspector::result_type inspect(Inspector& f, pending_msg& x) {
  return f(caf::meta::type_name("pending_msg"), x.atm, x.seq, x.content);
}

struct reliability_state {
  seq_num_t next = 0;
  std::vector<std::pair<pending_msg,retransmit_cnt>> pending;
  std::vector<pending_msg> early;
};

void print_on_exit(const caf::actor& hdl, const std::string& name);

caf::behavior ping(caf::event_based_actor* self, size_t num_pings);
caf::behavior pong();

// utility function for sending an integer type
template <class T>
void write_int(caf::io::stateful_broker<reliability_state>* self,
               caf::io::connection_handle hdl, T value) {
  using unsigned_type = typename std::make_unsigned<T>::type;
  auto cpy = static_cast<T>(htonl(static_cast<unsigned_type>(value)));
  self->write(hdl, sizeof(T), &cpy);
  self->flush(hdl);
}
void write_int(caf::io::stateful_broker<reliability_state>* self, 
               caf::io::connection_handle hdl, uint64_t value);

// utility function for reading an integer from incoming data
template <class T>
void read_int(const void* data, T& storage) {
  using unsigned_type = typename std::make_unsigned<T>::type;
  memcpy(&storage, data, sizeof(T));
  storage = static_cast<T>(ntohl(static_cast<unsigned_type>(storage)));
}
void read_int(const void* data, uint64_t& storage);

caf::behavior broker_impl(caf::io::stateful_broker<reliability_state>* self, 
                          caf::io::connection_handle hdl, 
                          const caf::actor& buddy);
caf::behavior server(caf::io::stateful_broker<reliability_state>* self,
                     const caf::actor& buddy);

} // namespace relm
