#include "builtins/builtin.hh"

#include <string>
#include <vector>

namespace jsmm::builtins {

namespace {

using types::TypeKind;

bool formattable(InferContext &context, const types::TypePtr &type) {
  const types::TypePtr resolved = context.resolveType(type);
  if (resolved == nullptr) return true;
  switch (resolved->kind) {
  case TypeKind::Number:
  case TypeKind::String:
  case TypeKind::Boolean:
  case TypeKind::Null:
  case TypeKind::Variable: return true;
  case TypeKind::Array: return formattable(context, resolved->element);
  case TypeKind::Union: {
    for (const types::TypePtr &member : resolved->members)
      if (!formattable(context, member)) return false;
    return true;
  }
  default: return false;
  }
}

TypePtr inferCat(InferContext &context, const std::vector<types::TypePtr> &arguments,
                 const SourceRange &range) {
  for (const auto &argument : arguments) {
    if (!formattable(context, argument))
      context.report(range,
                     "cat() cannot format a value of type `" + context.showType(argument) + "'");
    context.requireConcreteType(argument, range);
    context.deferCheck([&context, argument, range]() {
      if (formattable(context, argument)) return;
      context.report(range,
                     "cat() cannot format a value of type `" + context.showType(argument) + "'");
    });
  }
  return context.undefinedType();
}

void emitCat(EmitContext &context, const CallExpr &call) {
  for (const auto &argument : call.arguments) context.formatValue(argument);
  context.formatNewline();
  context.clearResult();
}

} // namespace

void declareCat(BuiltinRegistry &registry) {
  registry.declare({"cat", true, 0, inferCat, emitCat});
}

} // namespace jsmm::builtins
