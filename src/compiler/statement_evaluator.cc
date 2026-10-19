#include "statement_evaluator.hh"
#include "expression_evaluator.hh"

#include "runtime/operations.hh"

#include <stdexcept>

namespace rma::compiler {

StatementEvaluator::StatementEvaluator(EvaluatorContext &context) : context_(context) {}

void StatementEvaluator::executeStatement(const Statement &statement) {
  std::visit(
      [this](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, VariableDeclaration>)
          return executeVariableDeclaration(node);
        if constexpr (std::is_same_v<T, ExpressionStatement>)
          return executeExpressionStatement(node);
        if constexpr (std::is_same_v<T, FunctionDeclaration>)
          return executeFunctionDeclaration(node);
        if constexpr (std::is_same_v<T, ReturnStatement>) return executeReturnStatement(node);
        if constexpr (std::is_same_v<T, BlockStatement>) return executeBlock(node);
        if constexpr (std::is_same_v<T, IfStatement>) return executeIf(node);
        if constexpr (std::is_same_v<T, WhileStatement>) return executeWhile(node);
        if constexpr (std::is_same_v<T, ForStatement>) return executeFor(node);
        if constexpr (std::is_same_v<T, BreakStatement>) return executeBreak(node);
        if constexpr (std::is_same_v<T, ContinueStatement>) return executeContinue(node);
        if constexpr (std::is_same_v<T, GenericStatement>) return executeGeneric(node);
      },
      statement.node);
}

void StatementEvaluator::executeVariableDeclaration(const VariableDeclaration &declaration) {
  for (const auto &item : declaration.declarations) {
    if (declaration.kind == "var") {
      if (!context_.hasBinding(item.name)) {
        context_.createBinding(item.name, rma::syntax::Value::undefined(), true, "var");
      } else {
      }
      if (item.initializer) {
        context_.setBinding(item.name, context_.expressions().evaluateExpression(item.initializer),
                            false);
      }
      continue;
    }

    if (context_.hasBinding(item.name)) throw std::runtime_error("duplicate binding: " + item.name);

    rma::syntax::Value value = item.initializer
                                   ? context_.expressions().evaluateExpression(item.initializer)
                                   : rma::syntax::Value::undefined();
    context_.createBinding(item.name, std::move(value), declaration.kind != "const",
                           declaration.kind);
  }
}

void StatementEvaluator::executeExpressionStatement(const ExpressionStatement &statement) {
  (void)context_.expressions().evaluateExpression(statement.expression);
}

void StatementEvaluator::executeFunctionDeclaration(const FunctionDeclaration &) {
  throw std::runtime_error("backend does not yet execute function declarations");
}

void StatementEvaluator::executeReturnStatement(const ReturnStatement &) {
  throw std::runtime_error("return outside an executable function is not supported by backend");
}

void StatementEvaluator::executeBlock(const BlockStatement &block) {
  for (const auto &statement : block.statements) executeStatement(statement);
}

void StatementEvaluator::executeIf(const IfStatement &statement) {
  if (truthy(context_.expressions().evaluateExpression(statement.condition))) {
    for (const auto &child : statement.consequent) executeStatement(child);
  } else {
    for (const auto &child : statement.alternate) executeStatement(child);
  }
}

void StatementEvaluator::executeWhile(const WhileStatement &statement) {
  while (truthy(context_.expressions().evaluateExpression(statement.condition))) {
    try {
      for (const auto &child : statement.body) executeStatement(child);
    } catch (const BreakException &) { break; } catch (const ContinueException &) {
      continue;
    }
  }
}

void StatementEvaluator::executeFor(const ForStatement &statement) {
  if (statement.init) executeVariableDeclaration(*statement.init);

  while (!statement.condition ||
         truthy(context_.expressions().evaluateExpression(statement.condition))) {
    try {
      for (const auto &child : statement.body) executeStatement(child);
    } catch (const BreakException &) { break; } catch (const ContinueException &) {
      if (statement.update) (void)context_.expressions().evaluateExpression(statement.update);
      continue;
    }
    if (statement.update) (void)context_.expressions().evaluateExpression(statement.update);
  }
}

void StatementEvaluator::executeBreak(const BreakStatement &) { throw BreakException{}; }

void StatementEvaluator::executeContinue(const ContinueStatement &) { throw ContinueException{}; }

void StatementEvaluator::executeGeneric(const GenericStatement &statement) {
  throw std::runtime_error("unsupported statement node: " + statement.kind);
}

} // namespace rma::compiler
