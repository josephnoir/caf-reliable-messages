#include <iostream>

#ifdef WIN32
# define _WIN32_WINNT 0x0600
# include <Winsock2.h>
#else
# include <arpa/inet.h> // htonl
#endif

#include <caf/all.hpp>
#include <caf/config.hpp>

#include <caf/io/all.hpp>
#include <caf/io/middleman.hpp>

#include "include/reliable_broker.hpp"

using namespace std;
using namespace caf;
using namespace relm;

class config : public actor_system_config {
public:
  uint16_t port = 0;
  std::string host = "localhost";
  bool server_mode = false;

  config() {
    opt_group{custom_options_, "global"}
    .add(port, "port,p", "set port")
    .add(host, "host,H", "set host (ignored in server mode)")
    .add(server_mode, "server-mode,s", "enable server mode");
  }
};

void caf_main(actor_system& system, const config& cfg) {
  if (cfg.server_mode) {
    cout << "run in server mode" << endl;
    auto pong_actor = system.spawn(pong);
    auto server_actor = system.middleman().spawn_server(server, cfg.port,
                                                        pong_actor);
    if (!server_actor) {
      std::cerr << "failed to spawn server: "
                << system.render(server_actor.error()) << endl;
      return;
    }
    print_on_exit(*server_actor, "server");
    print_on_exit(pong_actor, "pong");
    return;
  }
  auto ping_actor = system.spawn(ping, size_t{100});
  auto io_actor = system.middleman().spawn_client(broker_impl, cfg.host,
                                                  cfg.port, ping_actor);
  if (!io_actor) {
    std::cerr << "failed to spawn client: "
               << system.render(io_actor.error()) << endl;
    return;
  }
  print_on_exit(ping_actor, "ping");
  print_on_exit(*io_actor, "protobuf_io");
  send_as(*io_actor, ping_actor, kickoff_atom::value, *io_actor);
}

CAF_MAIN(io::middleman)
