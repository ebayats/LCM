// Albany 3.0: Copyright 2016 National Technology & Engineering Solutions of
// Sandia, LLC (NTESS). This Software is released under the BSD license detailed
// in the file license.txt in the top-level Albany directory.

// @HEADER

#include "CounterMonitor.hpp"

namespace util {

CounterMonitor::CounterMonitor()
{
  title_          = "CounterMonitor";
  itemTypeLabel_  = "Counter";
  itemValueLabel_ = "Value";
}

string
CounterMonitor::getStringValue(const monitored_type& val)
{
  return std::to_string(static_cast<long long>(val.value()));
}

}  // namespace util
