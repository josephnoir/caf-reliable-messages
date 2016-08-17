
#include <iostream>

#include "include/utility.hpp"

namespace relm {

void print_on_exit(const caf::actor& hdl, const std::string& name) {
  hdl->attach_functor([=](const caf::error& reason) {
    std::cout << name << " exited: " << caf::to_string(reason) << std::endl;
  });
}

} // namespace relm
