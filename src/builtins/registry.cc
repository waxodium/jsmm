#include "builtins/builtin.hh"

namespace rma::builtins {

BuiltinRegistry &BuiltinRegistry::instance() {
  static BuiltinRegistry registry;
  return registry;
}

void BuiltinRegistry::registerAll() { declareCat(instance()); }

void BuiltinRegistry::declare(BuiltinSpec spec) { builtins_.push_back(std::move(spec)); }

const BuiltinSpec *BuiltinRegistry::find(const std::string &name) const {
  for (const auto &builtin : builtins_)
    if (builtin.name == name) return &builtin;
  return nullptr;
}

const std::vector<BuiltinSpec> &BuiltinRegistry::all() const { return builtins_; }

} // namespace rma::builtins
