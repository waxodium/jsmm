#include "builtins/builtin.hh"

#include <string>
#include <vector>

namespace jsmm::builtins {

namespace {

using types::TypeKind;

bool numeric(InferContext &context, const types::TypePtr &type) {
  const types::TypePtr resolved = context.resolveType(type);
  if (resolved == nullptr) return true;
  return resolved->kind == TypeKind::Number || resolved->kind == TypeKind::Integer ||
         resolved->kind == TypeKind::Byte;
}

bool pointer(InferContext &context, const types::TypePtr &type) {
  const types::TypePtr resolved = context.resolveType(type);
  if (resolved == nullptr) return true;
  return resolved->kind == TypeKind::Pointer || resolved->kind == TypeKind::Null;
}

TypePtr inferAlloc(InferContext &context, const std::vector<types::TypePtr> &arguments,
                   const SourceRange &range) {
  if (arguments.size() != 1)
    context.report(range, "alloc() requires exactly 1 argument (number of bytes)");
  if (!numeric(context, arguments[0]))
    context.report(range, "alloc() argument must be a number, got `" +
                              context.showType(arguments[0]) + "'");
  context.requireConcreteType(arguments[0], range);
  return context.bytePointerType();
}

TypePtr inferFree(InferContext &context, const std::vector<types::TypePtr> &arguments,
                  const SourceRange &range) {
  if (arguments.size() != 1)
    context.report(range, "free() requires exactly 1 argument (pointer)");
  if (!pointer(context, arguments[0]))
    context.report(range, "free() argument must be a pointer, got `" +
                              context.showType(arguments[0]) + "'");
  return context.undefinedType();
}

TypePtr inferMemcpy(InferContext &context, const std::vector<types::TypePtr> &arguments,
                    const SourceRange &range) {
  if (arguments.size() != 3)
    context.report(range, "memcpy() requires exactly 3 arguments (dest, src, n)");
  if (!pointer(context, arguments[0]))
    context.report(range, "memcpy() first argument (dest) must be a pointer, got `" +
                              context.showType(arguments[0]) + "'");
  if (!pointer(context, arguments[1]))
    context.report(range, "memcpy() second argument (src) must be a pointer, got `" +
                              context.showType(arguments[1]) + "'");
  if (!numeric(context, arguments[2]))
    context.report(range, "memcpy() third argument (n) must be a number, got `" +
                              context.showType(arguments[2]) + "'");
  return arguments[0];
}

TypePtr inferMemcmp(InferContext &context, const std::vector<types::TypePtr> &arguments,
                    const SourceRange &range) {
  if (arguments.size() != 3)
    context.report(range, "memcmp() requires exactly 3 arguments (a, b, n)");
  if (!pointer(context, arguments[0]))
    context.report(range, "memcmp() first argument (a) must be a pointer, got `" +
                                context.showType(arguments[0]) + "'");
  if (!pointer(context, arguments[1]))
    context.report(range, "memcmp() second argument (b) must be a pointer, got `" +
                                context.showType(arguments[1]) + "'");
  if (!numeric(context, arguments[2]))
    context.report(range, "memcmp() third argument (n) must be a number, got `" +
                                context.showType(arguments[2]) + "'");
  return context.numberType();
}

TypePtr inferMemset(InferContext &context, const std::vector<types::TypePtr> &arguments,
                    const SourceRange &range) {
  if (arguments.size() != 3)
    context.report(range, "memset() requires exactly 3 arguments (dest, c, n)");
  if (!pointer(context, arguments[0]))
    context.report(range, "memset() first argument (dest) must be a pointer, got `" +
                              context.showType(arguments[0]) + "'");
  if (!numeric(context, arguments[1]))
    context.report(range, "memset() second argument (c) must be a number, got `" +
                              context.showType(arguments[1]) + "'");
  if (!numeric(context, arguments[2]))
    context.report(range, "memset() third argument (n) must be a number, got `" +
                              context.showType(arguments[2]) + "'");
  return arguments[0];
}

void emitAlloc(EmitContext &context, const CallExpr &call) {
  if (call.arguments.size() != 1) {
    context.clearResult();
    return;
  }
  context.emitAllocation(call.arguments[0]);
}

void emitFree(EmitContext &context, const CallExpr &) { context.emitVoidResult(); }

void emitMemcpy(EmitContext &context, const CallExpr &call) {
  if (call.arguments.size() != 3) {
    context.clearResult();
    return;
  }
  context.emitMemcpy(call.arguments[0], call.arguments[1], call.arguments[2]);
}

void emitMemset(EmitContext &context, const CallExpr &call) {
  if (call.arguments.size() != 3) {
    context.clearResult();
    return;
  }
  context.emitMemset(call.arguments[0], call.arguments[1], call.arguments[2]);
}

void emitMemcmp(EmitContext &context, const CallExpr &call) {
  if (call.arguments.size() != 3) {
    context.clearResult();
    return;
  }
  context.emitMemcmp(call.arguments[0], call.arguments[1], call.arguments[2]);
}

} // namespace

void declareAlloc(BuiltinRegistry &registry) {
  registry.declare({"alloc", false, 1, {}, inferAlloc, emitAlloc});
}

void declareFree(BuiltinRegistry &registry) {
  registry.declare({"free", false, 1, {}, inferFree, emitFree});
}

void declareMemcpy(BuiltinRegistry &registry) {
  registry.declare({"memcpy", false, 3, {}, inferMemcpy, emitMemcpy});
}

void declareMemset(BuiltinRegistry &registry) {
  registry.declare({"memset", false, 3, {}, inferMemset, emitMemset});
}

void declareMemcmp(BuiltinRegistry &registry) {
  registry.declare({"memcmp", false, 3, {}, inferMemcmp, emitMemcmp});
}

} // namespace jsmm::builtins
