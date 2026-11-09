#pragma once

#include "reader/ast.hh"
#include "types/types.hh"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace jsmm::builtins {

using reader::CallExpr;
using reader::ExprPtr;
using reader::SourceRange;
using types::TypePtr;

class InferContext {
public:
  virtual ~InferContext() = default;

  virtual TypePtr freshVariable() = 0;
  virtual TypePtr stringType() = 0;
  virtual TypePtr numberType() = 0;
  virtual TypePtr booleanType() = 0;
  virtual TypePtr undefinedType() = 0;
  virtual TypePtr usizeType() = 0;
  virtual TypePtr bytePointerType() = 0;
  virtual TypePtr resolveType(TypePtr type) = 0;
  [[nodiscard]] virtual std::string showType(TypePtr type) = 0;
  [[noreturn]] virtual void report(const SourceRange &range, const std::string &message) = 0;
  virtual void requireUnify(TypePtr left, TypePtr right, const SourceRange &range,
                            const std::string &what) = 0;
  virtual void requireConcreteType(TypePtr type, const SourceRange &range) = 0;
  virtual void deferCheck(std::function<void()> check) = 0;
};

class EmitContext {
public:
  virtual ~EmitContext() = default;

  virtual void emitArgument(const ExprPtr &argument) = 0;
  virtual void moveResultToArgumentRegister() = 0;
  virtual void clearResult() = 0;
  virtual void declareHelper(std::string symbol, std::string text) = 0;
  virtual void useHelper(const std::string &symbol) = 0;
  virtual void callHelper(const std::string &symbol) = 0;
  virtual void emitSyscall(const CallExpr &call) = 0;
  virtual TypePtr operandType(const ExprPtr &operand) = 0;
  virtual void emitNumberResult(double value) = 0;
  virtual void emitAllocation(const ExprPtr &size) = 0;
  virtual void emitVoidResult() = 0;
  virtual void emitFree(const ExprPtr &ptr) = 0;
  virtual void emitMemcpy(const ExprPtr &dest, const ExprPtr &src,
                          const ExprPtr &count) = 0;
  virtual void emitMemset(const ExprPtr &dest, const ExprPtr &value,
                          const ExprPtr &count) = 0;
  virtual void emitMemcmp(const ExprPtr &a, const ExprPtr &b,
                          const ExprPtr &count) = 0;
  virtual void formatValue(const ExprPtr &expression) = 0;
  virtual void formatArray(const ExprPtr &expression) = 0;
  virtual void formatNewline() = 0;
};

enum class OperandKind { Value, Type };

struct BuiltinSpec {
  std::string name;
  bool variadic = false;
  std::size_t minimumArguments = 0;
  std::vector<OperandKind> operands;
  std::function<TypePtr(InferContext &, const std::vector<TypePtr> &, const SourceRange &)> infer;
  std::function<void(EmitContext &, const CallExpr &)> emit;
};

class BuiltinRegistry final {
public:
  static BuiltinRegistry &instance();
  static void registerAll();

  void declare(BuiltinSpec spec);
  [[nodiscard]] const BuiltinSpec *find(const std::string &name) const;
  [[nodiscard]] const std::vector<BuiltinSpec> &all() const;

private:
  std::vector<BuiltinSpec> builtins_;
};

void declareCat(BuiltinRegistry &registry);
void declareSyscall(BuiltinRegistry &registry);
void declareSizeof(BuiltinRegistry &registry);
void declareAlignof(BuiltinRegistry &registry);
void declareAlloc(BuiltinRegistry &registry);
void declareFree(BuiltinRegistry &registry);
void declareMemcpy(BuiltinRegistry &registry);
void declareMemset(BuiltinRegistry &registry);
void declareMemcmp(BuiltinRegistry &registry);

} // namespace jsmm::builtins
