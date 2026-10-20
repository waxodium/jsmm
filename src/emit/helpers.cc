#include "emit/helpers.hh"

#include <stdexcept>

namespace rma::emit {

void HelperRegistry::declare(std::string symbol, std::string text) {
  if (available_.find(symbol) != available_.end()) return;
  order_.push_back(symbol);
  available_.emplace(std::move(symbol), std::move(text));
}

void HelperRegistry::use(const std::string &symbol) {
  if (available_.find(symbol) == available_.end())
    throw std::runtime_error("unknown runtime helper: " + symbol);
  for (const auto &name : used_)
    if (name == symbol) return;
  used_.push_back(symbol);
}

bool HelperRegistry::used(const std::string &symbol) const {
  for (const auto &name : used_)
    if (name == symbol) return true;
  return false;
}

void HelperRegistry::reset() { used_.clear(); }

std::string HelperRegistry::assemble() const {
  std::string text;
  for (const auto &name : order_)
    if (used(name)) text += available_.at(name);
  return text;
}

}
