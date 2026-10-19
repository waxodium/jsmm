#pragma once

#include "ast.hh"
#include "evaluator.hh"

namespace rma::compiler {

struct BreakException {};
struct ContinueException {};

class StatementEvaluator final {
public:
  explicit StatementEvaluator(EvaluatorContext &context);

  void executeStatement(const Statement &statement);

private:
  EvaluatorContext &context_;

  void executeVariableDeclaration(const VariableDeclaration &declaration);
  void executeExpressionStatement(const ExpressionStatement &statement);
  void executeFunctionDeclaration(const FunctionDeclaration &function);
  void executeReturnStatement(const ReturnStatement &statement);
  void executeBlock(const BlockStatement &block);
  void executeIf(const IfStatement &statement);
  void executeWhile(const WhileStatement &statement);
  void executeFor(const ForStatement &statement);
  void executeBreak(const BreakStatement &statement);
  void executeContinue(const ContinueStatement &statement);
  void executeGeneric(const GenericStatement &statement);
};

} // namespace rma::compiler
