#pragma once

#include "ast.hh"
#include "engine/reference.hh"
#include "evaluator.hh"

#include <memory>

namespace rma::compiler {

class ExpressionEvaluator final {
public:
  explicit ExpressionEvaluator(EvaluatorContext &context);

  rma::syntax::Value evaluateExpression(const ExprPtr &expression);

private:
  EvaluatorContext &context_;

  rma::syntax::Value evaluateLiteral(const LiteralExpr &literal);
  rma::syntax::Value evaluateIdentifier(const IdentifierExpr &identifier);
  rma::syntax::Value evaluateUnary(const UnaryExpr &unary);
  rma::syntax::Value evaluateBinary(const BinaryExpr &expr);
  rma::syntax::Value evaluateAssignment(const AssignmentExpr &assignment);
  rma::syntax::Value evaluateUpdate(const UpdateExpr &update);
  rma::syntax::Value evaluateMember(const MemberExpr &member);
  rma::syntax::Value evaluateObject(const ObjectExpr &expr);
  rma::syntax::Value evaluateArray(const ArrayExpr &expr);
  rma::syntax::Value evaluateCall(const CallExpr &call);
  rma::syntax::Value evaluateGeneric(const GenericExpr &expr);

  rma::syntax::Value binaryOp(const std::string &op, const rma::syntax::Value &left,
                              const rma::syntax::Value &right);

  rma::syntax::Reference resolveMemberReference(const MemberExpr &member);
  rma::syntax::Reference resolveReference(const ExprPtr &target);
  rma::syntax::Value readReference(const rma::syntax::Reference &reference);
  rma::syntax::Value writeReference(const rma::syntax::Reference &reference,
                                    rma::syntax::Value value, bool allowImplicitBinding = false);
};

} // namespace rma::compiler
