#include "evaluator.hh"
#include "expression_evaluator.hh"
#include "statement_evaluator.hh"

#include "console.hh"
#include "engine/object.hh"

#include <stdexcept>
#include <utility>

namespace rma::compiler {

EvaluatorContext::EvaluatorContext()
    : exprEval_(std::make_unique<ExpressionEvaluator>(*this)),
      stmtEval_(std::make_unique<StatementEvaluator>(*this)) {
  createBinding("console", rma::syntax::Value(ConsoleBuiltins::namespaceObject()), false, "var");
}

EvaluatorContext::~EvaluatorContext() = default;

std::vector<std::pair<int, std::string>> EvaluatorContext::run(const Program &program) {
  for (const auto &statement : program.statements) {
    if (const auto *decl = std::get_if<VariableDeclaration>(&statement.node)) {
      if (decl->kind == "var") {
        for (const auto &item : decl->declarations) {
          if (!bindings_.contains(item.name)) {
            bindings_.emplace(item.name, Binding{rma::syntax::Value::undefined(), true, "var"});
          } else if (bindings_.at(item.name).kind != "var") {
            throw std::runtime_error("var declaration conflicts with lexical binding: " +
                                     item.name);
          }
        }
      }
    }
  }

  for (const auto &statement : program.statements) { execute(statement); }
  return output_;
}

rma::syntax::Value EvaluatorContext::evaluate(const ExprPtr &expression) {
  return exprEval_->evaluateExpression(expression);
}

void EvaluatorContext::execute(const Statement &statement) {
  stmtEval_->executeStatement(statement);
}

bool EvaluatorContext::getBinding(const std::string &name, rma::syntax::Value &out) const {
  const auto it = bindings_.find(name);
  if (it == bindings_.end()) return false;
  out = it->second.value;
  return true;
}

bool EvaluatorContext::setBinding(const std::string &name, rma::syntax::Value value, bool strict) {
  auto it = bindings_.find(name);
  if (it == bindings_.end()) {
    if (strict) return false;
    bindings_.emplace(name, Binding{std::move(value), true, "implicit"});
    return true;
  }
  if (!it->second.mutableBinding) return false;
  it->second.value = std::move(value);
  return true;
}

void EvaluatorContext::createBinding(const std::string &name, rma::syntax::Value value,
                                     bool mutableBinding, const std::string &kind) {
  bindings_.emplace(name, Binding{std::move(value), mutableBinding, kind});
}

bool EvaluatorContext::hasBinding(const std::string &name) const {
  return bindings_.contains(name);
}

void EvaluatorContext::captureOutput(int fd, std::string line) {
  output_.emplace_back(fd, std::move(line));
}

ExpressionEvaluator &EvaluatorContext::expressions() { return *exprEval_; }
StatementEvaluator &EvaluatorContext::statements() { return *stmtEval_; }

} // namespace rma::compiler
