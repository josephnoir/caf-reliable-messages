#pragma once
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

// utility function for sending an integer type
template <class T>
int write_int(caf::io::broker* self,
              caf::io::connection_handle hdl, T value) {
  using unsigned_type = typename std::make_unsigned<T>::type;
  auto cpy = static_cast<T>(htonl(static_cast<unsigned_type>(value)));
  self->write(hdl, sizeof(T), &cpy);
  self->flush(hdl);
  return sizeof(T);
}
int write_int(caf::io::broker* self,
              caf::io::connection_handle hdl, uint64_t value);

// utility function for reading an integer from incoming data
template <class T>
int read_int(const void* data, T& storage) {
  using unsigned_type = typename std::make_unsigned<T>::type;
  memcpy(&storage, data, sizeof(T));
  storage = static_cast<T>(ntohl(static_cast<unsigned_type>(storage)));
  return sizeof(T);
}
int read_int(const void* data, uint64_t& storage);

caf::behavior broker_impl(caf::io::broker* self,
                          caf::io::connection_handle hdl,
                          const caf::actor& buddy);
caf::behavior server(caf::io::broker* self,
                     const caf::actor& buddy);

} // namespace relm
