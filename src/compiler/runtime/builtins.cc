#include "builtins.hh"

#include "methods/array.hh"

namespace rma::compiler {

std::optional<rma::syntax::Value> invokeBuiltinMethod(const rma::syntax::Value &receiver,
                                                      const std::string &method,
                                                      const std::vector<rma::syntax::Value> &args) {
  if (receiver.isObject() && receiver.asObject()) {
    if (receiver.asObject()->kind == rma::syntax::ObjectKind::Array) {
      if (method == "push") return arrayPush(receiver, args);
      if (method == "pop") return arrayPop(receiver, args);
    }
  }
  return std::nullopt;
}

void initializeBuiltins() {
}

} // namespace rma::compiler
