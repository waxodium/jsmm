#include "builtins/builtin.hh"

#include <string>
#include <vector>

namespace jsmm::builtins {

namespace {

using types::TypeKind;

bool passable(InferContext &context, const types::TypePtr &type) {
  const types::TypePtr resolved = context.resolveType(type);
  if (resolved == nullptr) return true;
  switch (resolved->kind) {
  case TypeKind::Number:
  case TypeKind::Integer:
  case TypeKind::Byte:
  case TypeKind::Pointer:
  case TypeKind::Variable: return true;
  default: return false;
  }
}

TypePtr inferSyscall(InferContext &context, const std::vector<types::TypePtr> &arguments,
                     const SourceRange &range) {
  if (arguments.size() > 7)
    context.report(range, "syscall() accepts at most 7 arguments (syscall number plus 6 arguments)");
  for (const auto &argument : arguments) {
    if (!passable(context, argument))
      context.report(range, "syscall() arguments must be numbers or pointers, got `" +
                                context.showType(argument) + "'");
    context.requireConcreteType(argument, range);
  }
  return context.numberType();
}

void emitSyscall(EmitContext &context, const CallExpr &call) { context.emitSyscall(call); }

} // namespace

void declareSyscall(BuiltinRegistry &registry) {
  registry.declare({"syscall", true, 1, inferSyscall, emitSyscall});
}

} // namespace jsmm::builtins
