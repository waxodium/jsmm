#include "builtins/builtin.hh"

#include <string>
#include <vector>

namespace jsmm::builtins {

namespace {

using types::TypeKind;
using types::TypePtr;

bool passableVaType(InferContext &context, TypePtr type) {
  type = context.resolveType(type);
  if (type == nullptr) return false;
  switch (type->kind) {
  case TypeKind::Number:
  case TypeKind::Integer:
  case TypeKind::Byte:
  case TypeKind::Boolean:
  case TypeKind::Pointer:
  case TypeKind::Variable: return true;
  default: return false;
  }
}

TypePtr inferVaCount(InferContext &context, const std::vector<TypePtr> &arguments,
                     const SourceRange &range) {
  if (arguments.size() != 0)
    context.report(range, "va_count() takes no arguments");
  return context.numberType();
}

TypePtr inferVaArg(InferContext &context, const std::vector<TypePtr> &arguments,
                   const SourceRange &range) {
  if (arguments.size() != 2)
    context.report(range, "va_arg() requires exactly 2 arguments (index, type)");
  if (!passableVaType(context, arguments[0]))
    context.report(range, "va_arg() first argument (index) must be a number");
  if (!passableVaType(context, arguments[1]))
    context.report(range, "va_arg() second argument (type) must be a passable type "
                          "(int, int<N,signed|unsigned>, byte, bool, pointer, float, double)");
  return arguments[1];
}

void emitVaCount(EmitContext &context, const CallExpr &call) {
  (void)call;
  context.emitVaCount();
}

void emitVaArg(EmitContext &context, const CallExpr &call) {
  if (call.arguments.size() != 2) {
    context.clearResult();
    return;
  }
  context.emitVaArg(call);
}

} // namespace

void declareVaCount(BuiltinRegistry &registry) {
  registry.declare({"va_count", false, 0, {}, inferVaCount, emitVaCount});
}

void declareVaArg(BuiltinRegistry &registry) {
  registry.declare(
      {"va_arg", false, 2, {OperandKind::Value, OperandKind::Type}, inferVaArg, emitVaArg});
}

} // namespace jsmm::builtins
