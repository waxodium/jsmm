#include "builtins/builtin.hh"

#include <string>
#include <vector>

namespace rma::builtins {

namespace {

constexpr const char *catSymbol = "rma_cat_str";

constexpr const char *catHelperText = R"ASM(.section .text
.globl rma_cat_str
rma_cat_str:
  mov rdx, QWORD PTR [rdi]
  lea rsi, [rdi + 8]
  mov eax, 1
  mov edi, 1
  syscall
  ret
)ASM";

TypePtr inferCat(InferContext &context, const std::vector<TypePtr> &arguments,
                 const SourceRange &range) {
  for (const auto &argument : arguments)
    context.requireUnify(argument, context.stringType(), range, "cat() arguments must be strings");
  return context.undefinedType();
}

void emitCat(EmitContext &context, const CallExpr &call) {
  context.declareHelper(catSymbol, catHelperText);
  context.useHelper(catSymbol);
  for (const auto &argument : call.arguments) {
    context.emitArgument(argument);
    context.moveResultToArgumentRegister();
    context.callHelper(catSymbol);
  }
  context.clearResult();
}

}

void declareCat(BuiltinRegistry &registry) { registry.declare({"cat", true, 0, inferCat, emitCat}); }

}
