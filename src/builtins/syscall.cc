#include "builtins/builtin.hh"

#include <cstdint>
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

TypePtr inferLayout(InferContext &context, const std::vector<types::TypePtr> &arguments,
                    const SourceRange &range, const std::string &name) {
  if (arguments.size() != 1)
    context.report(range, name + "() requires exactly 1 argument");
  const types::TypeLayout layout = types::layoutOf(context.resolveType(arguments[0]));
  if (!layout.defined)
    context.report(range, name + "() is not defined for `" + context.showType(arguments[0]) + "'");
  return context.usizeType();
}

TypePtr inferSizeof(InferContext &context, const std::vector<types::TypePtr> &arguments,
                    const SourceRange &range) {
  return inferLayout(context, arguments, range, "sizeof");
}

TypePtr inferAlignof(InferContext &context, const std::vector<types::TypePtr> &arguments,
                     const SourceRange &range) {
  return inferLayout(context, arguments, range, "alignof");
}

void emitLayout(EmitContext &context, const CallExpr &call, bool alignment) {
  if (call.arguments.empty()) {
    context.clearResult();
    return;
  }
  const types::TypeLayout layout = types::layoutOf(context.operandType(call.arguments[0]));
  context.emitNumberResult(static_cast<double>(alignment ? layout.alignment : layout.size));
}

void emitSizeof(EmitContext &context, const CallExpr &call) { emitLayout(context, call, false); }

void emitAlignof(EmitContext &context, const CallExpr &call) { emitLayout(context, call, true); }

void emitSyscallExpression(EmitContext &context, const CallExpr &call) {
  context.emitSyscall(call);
}

} // namespace

void declareSyscall(BuiltinRegistry &registry) {
  registry.declare({"syscall", true, 1, {}, inferSyscall, emitSyscallExpression});
}

void declareSizeof(BuiltinRegistry &registry) {
  registry.declare({"sizeof", false, 1, {OperandKind::Type}, inferSizeof, emitSizeof});
}

void declareAlignof(BuiltinRegistry &registry) {
  registry.declare({"alignof", false, 1, {OperandKind::Type}, inferAlignof, emitAlignof});
}

} // namespace jsmm::builtins
