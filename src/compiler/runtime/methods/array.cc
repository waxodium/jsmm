#include "array.hh"

#include "engine/object.hh"

namespace rma::compiler {

rma::syntax::Value arrayPush(const rma::syntax::Value &receiver,
                             const std::vector<rma::syntax::Value> &args) {
  if (!receiver.isObject() || !receiver.asObject() ||
      receiver.asObject()->kind != rma::syntax::ObjectKind::Array)
    throw std::runtime_error("Array.prototype.push called on non-array receiver");

  auto arr = std::static_pointer_cast<rma::syntax::ArrayObject>(receiver.asObject());
  for (const auto &arg : args) arr->elements.push_back(arg);
  return rma::syntax::Value(static_cast<double>(arr->elements.size()));
}

rma::syntax::Value arrayPop(const rma::syntax::Value &receiver,
                            const std::vector<rma::syntax::Value> & /*args*/) {
  if (!receiver.isObject() || !receiver.asObject() ||
      receiver.asObject()->kind != rma::syntax::ObjectKind::Array)
    throw std::runtime_error("Array.prototype.pop called on non-array receiver");

  auto arr = std::static_pointer_cast<rma::syntax::ArrayObject>(receiver.asObject());
  if (arr->elements.empty()) return rma::syntax::Value::undefined();
  rma::syntax::Value last = arr->elements.back();
  arr->elements.pop_back();
  return last;
}

} // namespace rma::compiler
