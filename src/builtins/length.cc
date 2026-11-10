#include "builtins/builtin.hh"

#include <string>
#include <vector>

namespace jsmm::builtins {

namespace {

using types::TypeKind;

bool isSized(InferContext &context, const types::TypePtr &type) {
  const types::TypePtr resolved = context.resolveType(type);
  if (resolved == nullptr) return false;
  switch (resolved->kind) {
  case TypeKind::String:
  case TypeKind::Bytes:
  case TypeKind::Array: return true;
  default: return false;
  }
}

TypePtr inferLength(InferContext &context, const std::vector<types::TypePtr> &arguments,
                    const SourceRange &range) {
  if (arguments.size() != 1)
    context.report(range, "length() requires exactly 1 argument");
  if (!isSized(context, arguments[0]))
    context.report(range, "length() argument must be string, bytes, or array, got `" +
                                context.showType(arguments[0]) + "'");
  context.requireConcreteType(arguments[0], range);
  return context.numberType();
}

void emitLength(EmitContext &context, const CallExpr &call) {
  if (call.arguments.size() != 1) {
    context.clearResult();
    return;
  }
  context.emitLength(call.arguments[0]);
}

} // namespace

void declareLength(BuiltinRegistry &registry) {
  registry.declare({"length", false, 1, {}, inferLength, emitLength});
}

} // namespace jsmm::builtins
