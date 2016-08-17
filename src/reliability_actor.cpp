
#include <cassert>
#include <iostream>

#include "include/ping_pong.hpp"
#include "include/reliability_actor.hpp"

using namespace caf;
using namespace std;
using namespace std::chrono;

namespace relm {

namespace {
auto default_timeout = milliseconds(300);
}

using ack_atom = atom_constant<atom("ack")>;
using retransmit_atom = atom_constant<atom("retransmit")>;

reliable_msg ack_for(seq_num_t seq) {
  return reliable_msg{ack_atom::value, seq, 0};
}

string to_string(const reliable_msg& msg) {
  return "{" + to_string(msg.atm) + ", " + std::to_string(msg.seq) + 
         ", " + std::to_string(msg.content) + "}";
}

behavior boostrap_reliability_actor(stateful_actor<reliability_state>* self, const actor& buddy) {
  return {
    [=] (register_atom, const actor& broker) {
      self->become(reliability_actor(self, buddy, broker));
    }
  };
}

behavior reliability_actor(stateful_actor<reliability_state>* self, 
                           const actor& buddy, const actor& broker) {
  return {
    [=](ack_atom, seq_num_t seq) {
      auto& pending = self->state.pending;
      auto msg_itr = find_if(begin(pending), end(pending),
        [seq](const reliable_msg& elem){
          return elem.seq == seq;
        }
      );
      if (msg_itr != end(pending)) {
        aout(self) << "[" << seq << "] Received ACK." << endl;
        pending.erase(msg_itr);
      } else {
        aout(self) << "[" << seq << "] Received out of date ACK." << endl;
      }
    },
    [=](retransmit_atom, seq_num_t seq) {
      // May be easier to look for the next retransmit and set a timer
      // for it instead of using a separate timers for each possible retransmit.
      auto& pending = self->state.pending;
      auto msg_itr = find_if(begin(pending), end(pending),
        [seq](const reliable_msg& elem){
          return elem.seq == seq;
        }
      );
      if (msg_itr != end(pending)) {
        self->send(broker, send_atom::value, *msg_itr);
        aout(self) << "[" << seq << "] Retransmitting ..." << endl;
        self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      } else {
        aout(self) << "[" << seq << "] Already ACKed." << endl;
      }
    },
    [=](recv_atom, const reliable_msg& msg) {
      // Incoming message
      if (msg.atm == ping_atom::value || msg.atm == pong_atom::value) {
        // APPLICATION msg
        if (msg.seq < self->state.next) {
          aout(self) <<  "[" << msg.seq << "] Message " << to_string(msg)
                   << " already handled." << endl;
          self->send(broker, send_atom::value, ack_for(msg.seq));
        } else if (msg.seq == self->state.next) {
          aout(self) <<  "[" << msg.seq << "] Received " << to_string(msg) << endl;
          self->delayed_send(buddy, milliseconds(500), msg.atm, msg.content);
          self->send(broker, send_atom::value, ack_for(msg.seq));
          self->state.next += 1;
        } else if (msg.seq > self->state.next) {
          aout(self) <<  "[" << msg.seq << "] Message " << to_string(msg.atm)
                   << " was early, awaiting "
                   << self->state.next << "." << endl;
          self->state.early.push_back(msg);
        }  
      } else {
        // CONTROL msg
        self->send(self, msg.atm, msg.seq);
      }
    },
    [=](atom_value av, int32_t i) {
      // Message from ping actor, forward via our connection handle
      assert(av == ping_atom::value || av == pong_atom::value);
      seq_num_t seq = self->state.next;
      reliable_msg msg{av, seq, i};
      aout(self) << "[" << seq << "] Sending " << to_string(msg) << "." << endl;
      self->send(broker, send_atom::value, msg);
      self->state.pending.push_back(msg);
      self->delayed_send(self, default_timeout, retransmit_atom::value, seq);
      self->state.next += 1; // increase sequence number for next packet
    }
  };
}

} // namespace relm

