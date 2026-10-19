#pragma once

#include "ast.hh"
#include "engine/value.hh"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rma::compiler {

class ExpressionEvaluator;
class StatementEvaluator;

struct Binding {
  rma::syntax::Value value;
  bool mutableBinding = true;
  std::string kind;
};

class EvaluatorContext final {
public:
  EvaluatorContext();
  ~EvaluatorContext();

  std::vector<std::pair<int, std::string>> run(const Program &program);

  rma::syntax::Value evaluate(const ExprPtr &expression);
  void execute(const Statement &statement);

  bool getBinding(const std::string &name, rma::syntax::Value &out) const;
  bool setBinding(const std::string &name, rma::syntax::Value value, bool strict);
  void createBinding(const std::string &name, rma::syntax::Value value, bool mutableBinding,
                     const std::string &kind);
  bool hasBinding(const std::string &name) const;

  void captureOutput(int fd, std::string line);

  ExpressionEvaluator &expressions();
  StatementEvaluator &statements();

private:
  std::unordered_map<std::string, Binding> bindings_;
  std::vector<std::pair<int, std::string>> output_;
  std::unique_ptr<ExpressionEvaluator> exprEval_;
  std::unique_ptr<StatementEvaluator> stmtEval_;
};

} // namespace rma::compiler
