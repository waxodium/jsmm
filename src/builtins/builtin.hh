#pragma once

#include "reader/ast.hh"
#include "types/types.hh"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace rma::builtins {

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
  virtual TypePtr resolveType(TypePtr type) = 0;
  [[nodiscard]] virtual std::string showType(TypePtr type) = 0;
  [[noreturn]] virtual void report(const SourceRange &range, const std::string &message) = 0;
  virtual void requireUnify(TypePtr left, TypePtr right, const SourceRange &range,
                            const std::string &what) = 0;
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
  virtual void formatValue(const ExprPtr &expression) = 0;
  virtual void formatArray(const ExprPtr &expression) = 0;
  virtual void formatNewline() = 0;
};

struct BuiltinSpec {
  std::string name;
  bool variadic = false;
  std::size_t minimumArguments = 0;
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

} // namespace rma::builtins
