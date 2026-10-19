#include "expression_evaluator.hh"

#include "console.hh"
#include "engine/object.hh"
#include "engine/reference.hh"
#include "runtime/operations.hh"

#include <limits>
#include <stdexcept>
#include <string>

namespace rma::compiler {

ExpressionEvaluator::ExpressionEvaluator(EvaluatorContext &context) : context_(context) {}

rma::syntax::Value ExpressionEvaluator::evaluateExpression(const ExprPtr &expression) {
  if (!expression) return rma::syntax::Value::undefined();
  return std::visit(
      [this](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LiteralExpr>) return evaluateLiteral(node);
        if constexpr (std::is_same_v<T, IdentifierExpr>) return evaluateIdentifier(node);
        if constexpr (std::is_same_v<T, UnaryExpr>) return evaluateUnary(node);
        if constexpr (std::is_same_v<T, BinaryExpr>) return evaluateBinary(node);
        if constexpr (std::is_same_v<T, AssignmentExpr>) return evaluateAssignment(node);
        if constexpr (std::is_same_v<T, UpdateExpr>) return evaluateUpdate(node);
        if constexpr (std::is_same_v<T, MemberExpr>) return evaluateMember(node);
        if constexpr (std::is_same_v<T, ObjectExpr>) return evaluateObject(node);
        if constexpr (std::is_same_v<T, ArrayExpr>) return evaluateArray(node);
        if constexpr (std::is_same_v<T, CallExpr>) return evaluateCall(node);
        if constexpr (std::is_same_v<T, GenericExpr>) return evaluateGeneric(node);
      },
      expression->node);
}

rma::syntax::Value ExpressionEvaluator::evaluateLiteral(const LiteralExpr &literal) {
  return literalValue(literal);
}

rma::syntax::Value ExpressionEvaluator::evaluateIdentifier(const IdentifierExpr &identifier) {
  if (identifier.name == "NaN") return rma::syntax::Value(std::numeric_limits<double>::quiet_NaN());
  if (identifier.name == "Infinity")
    return rma::syntax::Value(std::numeric_limits<double>::infinity());
  rma::syntax::Value out;
  if (!context_.getBinding(identifier.name, out))
    throw std::runtime_error("unknown identifier: " + identifier.name);
  return out;
}

rma::syntax::Value ExpressionEvaluator::evaluateUnary(const UnaryExpr &unary) {
  const rma::syntax::Value value = evaluateExpression(unary.operand);
  if (unary.op == "+") return rma::syntax::Value(number(value));
  if (unary.op == "-") return rma::syntax::Value(-number(value));
  if (unary.op == "!") return rma::syntax::Value(!truthy(value));
  if (unary.op == "~") return rma::syntax::Value(static_cast<double>(~toInt32(value)));
  if (unary.op == "typeof") {
    if (value.isUndefined()) return rma::syntax::Value("undefined");
    if (value.isNull() || value.isObject()) return rma::syntax::Value("object");
    if (value.isBoolean()) return rma::syntax::Value("boolean");
    if (value.isNumber()) return rma::syntax::Value("number");
    if (value.isString()) return rma::syntax::Value("string");
    if (value.isSymbol()) return rma::syntax::Value("symbol");
  }
  if (unary.op == "void") return rma::syntax::Value::undefined();
  throw std::runtime_error("unsupported unary operator: " + unary.op);
}

rma::syntax::Value ExpressionEvaluator::evaluateBinary(const BinaryExpr &expr) {
  rma::syntax::Value left = evaluateExpression(expr.left);
  if (expr.op == "&&") return truthy(left) ? evaluateExpression(expr.right) : left;
  if (expr.op == "||") return truthy(left) ? left : evaluateExpression(expr.right);
  if (expr.op == "??") return left.isNullish() ? evaluateExpression(expr.right) : left;
  return binaryOp(expr.op, left, evaluateExpression(expr.right));
}

rma::syntax::Value ExpressionEvaluator::evaluateAssignment(const AssignmentExpr &assignment) {
  const auto reference = resolveReference(assignment.target);
  if (assignment.op == "=")
    return writeReference(reference, evaluateExpression(assignment.value), true);
  const rma::syntax::Value previous = readReference(reference);
  const rma::syntax::Value rhs = evaluateExpression(assignment.value);
  const std::string op = assignment.op.substr(0, assignment.op.size() - 1);
  return writeReference(reference, binaryOp(op, previous, rhs));
}

rma::syntax::Value ExpressionEvaluator::evaluateUpdate(const UpdateExpr &update) {
  const rma::syntax::Value previous = evaluateExpression(update.target);
  const double num = number(previous);
  const rma::syntax::Value next =
      (update.op == "++") ? rma::syntax::Value(num + 1.0) : rma::syntax::Value(num - 1.0);
  writeReference(resolveReference(update.target), next);
  return update.prefix ? next : previous;
}

rma::syntax::Value ExpressionEvaluator::evaluateMember(const MemberExpr &member) {
  return readReference(resolveMemberReference(member));
}

rma::syntax::Value ExpressionEvaluator::evaluateObject(const ObjectExpr &expr) {
  auto object = std::make_shared<rma::syntax::Object>();
  for (const auto &property : expr.properties) {
    if (!rma::syntax::ObjectOperations::defineDataProperty(
            object, rma::syntax::PropertyKey(property.key), evaluateExpression(property.value),
            true, true, true))
      throw std::runtime_error("cannot define object property: " + property.key);
  }
  return rma::syntax::Value(std::move(object));
}

rma::syntax::Value ExpressionEvaluator::evaluateArray(const ArrayExpr &expr) {
  auto array = std::make_shared<rma::syntax::ArrayObject>();
  for (const auto &element : expr.elements) {
    array->elements.push_back(evaluateExpression(element));
  }
  return rma::syntax::Value(std::move(array));
}

rma::syntax::Value ExpressionEvaluator::evaluateCall(const CallExpr &call) {
  if (const auto *identifier = std::get_if<IdentifierExpr>(&call.callee->node)) {
    if (identifier->name == "isNaN") {
      if (call.arguments.empty()) return rma::syntax::Value(true);
      return rma::syntax::Value(std::isnan(number(evaluateExpression(call.arguments.front()))));
    }
    throw std::runtime_error("unsupported function call: " + identifier->name);
  }

  const auto *member = std::get_if<MemberExpr>(&call.callee->node);
  if (member) {
    const rma::syntax::Value base = evaluateExpression(member->object);

    if (base.isObject() && base.asObject() &&
        base.asObject()->kind == rma::syntax::ObjectKind::Array) {
      auto arr = std::static_pointer_cast<rma::syntax::ArrayObject>(base.asObject());
      if (member->property == "push") {
        for (const auto &arg : call.arguments) arr->elements.push_back(evaluateExpression(arg));
        return rma::syntax::Value(static_cast<double>(arr->elements.size()));
      }
      if (member->property == "pop") {
        if (arr->elements.empty()) return rma::syntax::Value::undefined();
        rma::syntax::Value last = arr->elements.back();
        arr->elements.pop_back();
        return last;
      }
    }

    if (const auto *object = std::get_if<IdentifierExpr>(&member->object->node)) {
      if (object->name == "console") {
        std::string method = member->property;
        if (member->computed) {
          const rma::syntax::Value key = evaluateExpression(member->computedProperty);
          if (key.isSymbol()) throw std::runtime_error("console method name cannot be a Symbol");
          method = toString(key);
        }
        int fd = 1;
        if (method == "error" || method == "warn" || method == "warning") fd = 2;
        else if (method != "log" && method != "info" && method != "debug")
          throw std::runtime_error("unsupported console method: " + method);

        std::string line;
        for (std::size_t i = 0; i < call.arguments.size(); ++i) {
          if (i) line += ' ';
          line += formatConsoleValue(evaluateExpression(call.arguments[i]));
        }
        line += '\n';
        context_.captureOutput(fd, std::move(line));
        return rma::syntax::Value::undefined();
      }
    }
  }

  throw std::runtime_error("only console calls are executable in the current backend");
}

rma::syntax::Value ExpressionEvaluator::evaluateGeneric(const GenericExpr &expr) {
  throw std::runtime_error("unsupported expression node: " + expr.kind);
}

rma::syntax::Value ExpressionEvaluator::binaryOp(const std::string &op,
                                                 const rma::syntax::Value &left,
                                                 const rma::syntax::Value &right) {
  if (op == "+" && (left.isString() || right.isString() || left.isObject() || right.isObject()))
    return rma::syntax::Value(toString(left) + toString(right));
  if (op == "&") return rma::syntax::Value(static_cast<double>(toInt32(left) & toInt32(right)));
  if (op == "|") return rma::syntax::Value(static_cast<double>(toInt32(left) | toInt32(right)));
  if (op == "^") return rma::syntax::Value(static_cast<double>(toInt32(left) ^ toInt32(right)));
  if (op == "<<") {
    const auto shift = toUint32(right) & 31U;
    return rma::syntax::Value(static_cast<double>(
        static_cast<std::int32_t>(static_cast<std::uint32_t>(toInt32(left)) << shift)));
  }
  if (op == ">>")
    return rma::syntax::Value(static_cast<double>(toInt32(left) >> (toUint32(right) & 31U)));
  if (op == ">>>")
    return rma::syntax::Value(static_cast<double>(toUint32(left) >> (toUint32(right) & 31U)));

  const double a = number(left), b = number(right);
  if (op == "+") return rma::syntax::Value(a + b);
  if (op == "-") return rma::syntax::Value(a - b);
  if (op == "*") return rma::syntax::Value(a * b);
  if (op == "/") return rma::syntax::Value(a / b);
  if (op == "%") return rma::syntax::Value(std::fmod(a, b));
  if (op == "**") return rma::syntax::Value(std::pow(a, b));
  if (op == "<")
    return rma::syntax::Value(left.isString() && right.isString() ? toString(left) < toString(right)
                                                                  : a < b);
  if (op == ">")
    return rma::syntax::Value(left.isString() && right.isString() ? toString(left) > toString(right)
                                                                  : a > b);
  if (op == "<=")
    return rma::syntax::Value(
        left.isString() && right.isString() ? toString(left) <= toString(right) : a <= b);
  if (op == ">=")
    return rma::syntax::Value(
        left.isString() && right.isString() ? toString(left) >= toString(right) : a >= b);
  if (op == "==") return rma::syntax::Value(looseEqual(left, right));
  if (op == "===") return rma::syntax::Value(strictEqual(left, right));
  if (op == "!=") return rma::syntax::Value(!looseEqual(left, right));
  if (op == "!==") return rma::syntax::Value(!strictEqual(left, right));
  throw std::runtime_error("unsupported binary operator: " + op);
}

rma::syntax::Reference ExpressionEvaluator::resolveMemberReference(const MemberExpr &member) {
  const rma::syntax::Value base = evaluateExpression(member.object);
  if (!base.isObject() || !base.asObject())
    throw std::runtime_error("cannot access a property of a non-object value");
  rma::syntax::PropertyKey key = member.computed
                                     ? toPropertyKey(evaluateExpression(member.computedProperty))
                                     : rma::syntax::PropertyKey(member.property);
  return rma::syntax::Reference::property(base.asObject(), std::move(key));
}

rma::syntax::Reference ExpressionEvaluator::resolveReference(const ExprPtr &target) {
  if (!target) throw std::runtime_error("invalid assignment target");
  if (const auto *identifier = std::get_if<IdentifierExpr>(&target->node))
    return rma::syntax::Reference::binding(identifier->name);
  if (const auto *member = std::get_if<MemberExpr>(&target->node))
    return resolveMemberReference(*member);
  throw std::runtime_error("invalid assignment target");
}

rma::syntax::Value ExpressionEvaluator::readReference(const rma::syntax::Reference &reference) {
  if (reference.kind == rma::syntax::ReferenceKind::Binding) {
    rma::syntax::Value out;
    if (!context_.getBinding(reference.name, out))
      throw std::runtime_error("unknown identifier: " + reference.name);
    return out;
  }
  if (reference.kind == rma::syntax::ReferenceKind::Property)
    return rma::syntax::ObjectOperations::get(reference.base, *reference.key,
                                              rma::syntax::Value(reference.base));
  throw std::runtime_error("invalid reference");
}

rma::syntax::Value ExpressionEvaluator::writeReference(const rma::syntax::Reference &reference,
                                                       rma::syntax::Value value,
                                                       bool allowImplicitBinding) {
  if (reference.kind == rma::syntax::ReferenceKind::Binding) {
    if (!context_.hasBinding(reference.name)) {
      if (!allowImplicitBinding) throw std::runtime_error("unknown identifier: " + reference.name);
      context_.createBinding(reference.name, std::move(value), true, "implicit");
      return value;
    }
    if (!context_.setBinding(reference.name, std::move(value), false))
      throw std::runtime_error("assignment to constant: " + reference.name);
    return value;
  }
  if (reference.kind == rma::syntax::ReferenceKind::Property) {
    if (!rma::syntax::ObjectOperations::set(reference.base, *reference.key, value,
                                            rma::syntax::Value(reference.base)))
      throw std::runtime_error("cannot assign to object property");
    return value;
  }
  throw std::runtime_error("invalid reference");
}

} // namespace rma::compiler
