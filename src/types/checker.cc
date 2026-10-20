#include "types/checker.hh"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace rma::types {
namespace {

const char *kindName(TypeKind kind) {
  switch (kind) {
    case TypeKind::Number: return "number";
    case TypeKind::String: return "string";
    case TypeKind::Boolean: return "boolean";
    case TypeKind::Null: return "null";
    case TypeKind::Undefined: return "undefined";
    case TypeKind::Never: return "never";
    case TypeKind::Variable: return "variable";
    case TypeKind::Function: return "function";
    case TypeKind::Struct: return "struct";
    case TypeKind::Array: return "array";
    case TypeKind::Union: return "union";
  }
  return "unknown";
}

}

Checker::Checker() : unifier_(context_) {
  rootScope_.parent = nullptr;
  currentScope_ = &rootScope_;
}

TypeContext &Checker::context() { return context_; }

TypePtr Checker::freshVariable() { return context_.variable(); }
TypePtr Checker::stringType() { return context_.string(); }
TypePtr Checker::numberType() { return context_.number(); }
TypePtr Checker::booleanType() { return context_.boolean(); }
TypePtr Checker::undefinedType() { return context_.undefined(); }

void Checker::requireUnify(TypePtr left, TypePtr right, const reader::SourceRange &range,
                           const std::string &what) {
  const TypePtr leftCopy = left;
  const TypePtr rightCopy = right;
  if (!unifier_.unify(std::move(left), std::move(right)))
    fail(range, what + ": cannot unify `" + context_.show(leftCopy) + "' with `" +
                    context_.show(rightCopy) + "'");
}

Checker::Scope *Checker::pushScope(Scope *parent) {
  scopePool_.push_back(std::make_unique<Scope>());
  Scope *scope = scopePool_.back().get();
  scope->parent = parent;
  currentScope_ = scope;
  return scope;
}

void Checker::popScope() {
  currentScope_ = currentScope_->parent;
}

Checker::Binding *Checker::lookupBinding(const std::string &name) {
  for (Scope *scope = currentScope_; scope != nullptr; scope = scope->parent) {
    const auto found = scope->bindings.find(name);
    if (found != scope->bindings.end()) return &found->second;
  }
  return nullptr;
}

std::vector<TypePtr> Checker::environmentTypes() const {
  std::vector<TypePtr> types;
  for (const Scope *scope = currentScope_; scope != nullptr; scope = scope->parent)
    for (const auto &[name, binding] : scope->bindings) types.push_back(binding.scheme.type);
  return types;
}

[[noreturn]] void Checker::fail(const reader::SourceRange &range,
                                const std::string &message) const {
  throw std::runtime_error("type error at " + std::to_string(range.start.line) + ":" +
                           std::to_string(range.start.column) + ": " + message);
}

void Checker::requireKind(TypePtr type, TypeKind kind, const reader::SourceRange &range,
                          const std::string &what) {
  TypePtr target;
  switch (kind) {
    case TypeKind::Number: target = context_.number(); break;
    case TypeKind::Boolean: target = context_.boolean(); break;
    case TypeKind::String: target = context_.string(); break;
    case TypeKind::Undefined: target = context_.undefined(); break;
    default: break;
  }
  if (target != nullptr) {
    requireUnify(std::move(type), std::move(target), range, what);
    return;
  }
  type = context_.resolve(std::move(type));
  if (type == nullptr || type->kind != kind)
    fail(range, what + ": expected `" + std::string(kindName(kind)) + "', got `" +
                    context_.show(type) + "'");
}

TypePtr Checker::annotate(const reader::ExprPtr &expr, TypePtr type) {
  annotations_.expressionTypes.emplace(expr.get(), type);
  return type;
}

void Checker::hoistFunctions(const reader::Program &program,
                             std::vector<FunctionEntry> &functions) {
  for (const auto &statement : program.statements) {
    const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement.node);
    if (declaration == nullptr) continue;
    FunctionEntry entry;
    entry.declaration = declaration;
    entry.range = statement.range;
    for (std::size_t i = 0; i < declaration->parameters.size(); ++i)
      entry.parameters.push_back(context_.variable());
    entry.result = context_.variable();
    Binding binding;
    binding.scheme = TypeScheme{{}, context_.functionType(entry.parameters, entry.result)};
    binding.isConst = true;
    rootScope_.bindings.insert_or_assign(declaration->name, std::move(binding));
    functions.push_back(std::move(entry));
  }
}

void Checker::inferFunctionBody(const FunctionEntry &entry) {
  pushScope(&rootScope_);
  for (std::size_t i = 0; i < entry.declaration->parameters.size(); ++i) {
    Binding binding;
    binding.scheme = TypeScheme{{}, entry.parameters[i]};
    binding.isConst = true;
    currentScope_->bindings.emplace(entry.declaration->parameters[i], std::move(binding));
  }
  for (std::size_t i = 1; i < entry.parameters.size(); ++i)
    requireUnify(entry.parameters[i], entry.parameters[0], entry.range,
                 "function parameters must unify to a single type");
  inFunction_ = true;
  returnTargets_.push_back(entry.result);
  inferStatements(entry.declaration->body);
  returnTargets_.pop_back();
  inFunction_ = false;
  if (!definitelyReturns(entry.declaration->body))
    requireUnify(entry.result, context_.undefined(), entry.range,
                 "function without a guaranteed return unifies its result with undefined");
  annotations_.functionResults.emplace(entry.declaration, context_.resolve(entry.result));
  popScope();
}

void Checker::inferStatements(const reader::StatementList &statements) {
  for (const auto &statement : statements) inferStatement(statement);
}

void Checker::inferStatement(const reader::Statement &statement) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          for (const auto &declarator : node.declarations) {
            const TypePtr type = declarator.initializer != nullptr
                                     ? inferExpression(declarator.initializer)
                                     : context_.variable();
            Binding binding;
            binding.scheme = generalize(type, environmentTypes(), context_);
            binding.isConst = node.kind == "const";
            currentScope_->bindings.insert_or_assign(declarator.name, std::move(binding));
          }
        } else if constexpr (std::is_same_v<T, reader::ExpressionStatement>) {
          (void)inferExpression(node.expression);
        } else if constexpr (std::is_same_v<T, reader::FunctionDeclaration>) {
          if (inFunction_)
            fail(statement.range, "function declarations must appear at the top level");
        } else if constexpr (std::is_same_v<T, reader::ReturnStatement>) {
          if (returnTargets_.empty())
            fail(statement.range, "'return' outside of a function body");
          const TypePtr type = node.argument != nullptr ? inferExpression(node.argument)
                                                        : context_.undefined();
          requireUnify(returnTargets_.back(), type, statement.range, "return value");
        } else if constexpr (std::is_same_v<T, reader::BlockStatement>) {
          pushScope(currentScope_);
          inferStatements(node.statements);
          popScope();
        } else if constexpr (std::is_same_v<T, reader::IfStatement>) {
          requireKind(inferExpression(node.condition), TypeKind::Boolean, node.condition->range,
                      "if condition must be boolean");
          pushScope(currentScope_);
          inferStatements(node.consequent);
          popScope();
          if (!node.alternate.empty()) {
            pushScope(currentScope_);
            inferStatements(node.alternate);
            popScope();
          }
        } else if constexpr (std::is_same_v<T, reader::WhileStatement>) {
          requireKind(inferExpression(node.condition), TypeKind::Boolean, node.condition->range,
                      "while condition must be boolean");
          ++loopDepth_;
          pushScope(currentScope_);
          inferStatements(node.body);
          popScope();
          --loopDepth_;
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          pushScope(currentScope_);
          if (node.init != nullptr) inferStatement(reader::Statement{*node.init, statement.range});
          if (node.condition != nullptr)
            requireKind(inferExpression(node.condition), TypeKind::Boolean,
                        node.condition->range, "for condition must be boolean");
          if (node.update != nullptr) (void)inferExpression(node.update);
          ++loopDepth_;
          inferStatements(node.body);
          --loopDepth_;
          popScope();
        } else if constexpr (std::is_same_v<T, reader::BreakStatement>) {
          if (loopDepth_ == 0) fail(statement.range, "'break' outside of a loop");
        } else if constexpr (std::is_same_v<T, reader::ContinueStatement>) {
          if (loopDepth_ == 0) fail(statement.range, "'continue' outside of a loop");
        }
      },
      statement.node);
}

TypePtr Checker::inferExpression(const reader::ExprPtr &expr) {
  TypePtr type = std::visit(
      [&](const auto &node) -> TypePtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::NumberLiteralExpr>) {
          return context_.number();
        } else if constexpr (std::is_same_v<T, reader::StringLiteralExpr>) {
          return context_.string();
        } else if constexpr (std::is_same_v<T, reader::BooleanLiteralExpr>) {
          return context_.boolean();
        } else if constexpr (std::is_same_v<T, reader::NullLiteralExpr>) {
          return context_.null();
        } else if constexpr (std::is_same_v<T, reader::UndefinedLiteralExpr>) {
          return context_.undefined();
        } else if constexpr (std::is_same_v<T, reader::IdentifierExpr>) {
          return inferIdentifier(expr, node);
        } else if constexpr (std::is_same_v<T, reader::UnaryExpr>) {
          return inferUnary(expr, node);
        } else if constexpr (std::is_same_v<T, reader::BinaryExpr>) {
          return inferBinary(expr, node);
        } else if constexpr (std::is_same_v<T, reader::UpdateExpr>) {
          return inferUpdate(expr, node);
        } else if constexpr (std::is_same_v<T, reader::AssignmentExpr>) {
          return inferAssignment(expr, node);
        } else if constexpr (std::is_same_v<T, reader::MemberExpr>) {
          return inferMember(expr, node);
        } else if constexpr (std::is_same_v<T, reader::ObjectExpr>) {
          return inferObject(expr, node);
        } else if constexpr (std::is_same_v<T, reader::ArrayExpr>) {
          return inferArray(expr, node);
        } else if constexpr (std::is_same_v<T, reader::CallExpr>) {
          return inferCall(expr, node);
        }
        return context_.never();
      },
      expr->node);
  return annotate(expr, std::move(type));
}

TypePtr Checker::inferIdentifier(const reader::ExprPtr &expr,
                                 const reader::IdentifierExpr &identifier) {
  if (const Binding *binding = lookupBinding(identifier.name))
    return instantiate(binding->scheme, context_);
  if (builtins::BuiltinRegistry::instance().find(identifier.name) != nullptr)
    fail(expr->range, "builtin '" + identifier.name + "' cannot be used as a value");
  fail(expr->range, "undefined identifier '" + identifier.name + "'");
}

TypePtr Checker::inferUnary(const reader::ExprPtr &expr, const reader::UnaryExpr &unary) {
  if (unary.op == "typeof") {
    (void)inferExpression(unary.operand);
    return context_.string();
  }
  if (unary.op == "void") {
    (void)inferExpression(unary.operand);
    return context_.undefined();
  }
  if (unary.op == "delete")
    fail(expr->range, "'delete' is not supported in the static subset");
  const TypePtr operand = inferExpression(unary.operand);
  if (unary.op == "!") {
    requireKind(operand, TypeKind::Boolean, expr->range, "'!' requires a boolean operand");
    return context_.boolean();
  }
  requireKind(operand, TypeKind::Number, expr->range,
              "'" + unary.op + "' requires a number operand");
  return context_.number();
}

TypePtr Checker::inferBinary(const reader::ExprPtr &expr, const reader::BinaryExpr &binary) {
  const TypePtr left = inferExpression(binary.left);
  const TypePtr right = inferExpression(binary.right);
  static const std::vector<std::string> arithmetic = {"+", "-", "*", "/", "%", "**"};
  static const std::vector<std::string> bitwise = {"&", "|", "^", "<<", ">>", ">>>"};
  static const std::vector<std::string> relational = {"<", ">", "<=", ">="};
  static const std::vector<std::string> equality = {"==", "!=", "===", "!=="};
  for (const auto &op : arithmetic)
    if (binary.op == op) {
      requireKind(left, TypeKind::Number, binary.left->range,
                  "'" + binary.op + "' requires a number operand");
      requireKind(right, TypeKind::Number, binary.right->range,
                  "'" + binary.op + "' requires a number operand");
      return context_.number();
    }
  for (const auto &op : bitwise)
    if (binary.op == op) {
      requireKind(left, TypeKind::Number, binary.left->range,
                  "'" + binary.op + "' requires a number operand");
      requireKind(right, TypeKind::Number, binary.right->range,
                  "'" + binary.op + "' requires a number operand");
      return context_.number();
    }
  for (const auto &op : relational)
    if (binary.op == op) {
      requireKind(left, TypeKind::Number, binary.left->range,
                  "'" + binary.op + "' requires number operands");
      requireKind(right, TypeKind::Number, binary.right->range,
                  "'" + binary.op + "' requires number operands");
      return context_.boolean();
    }
  for (const auto &op : equality)
    if (binary.op == op) {
      requireUnify(left, right, expr->range, "equality operands must have the same type");
      return context_.boolean();
    }
  if (binary.op == "&&" || binary.op == "||") {
    requireKind(left, TypeKind::Boolean, binary.left->range,
                "'" + binary.op + "' requires a boolean left operand");
    requireKind(right, TypeKind::Boolean, binary.right->range,
                "'" + binary.op + "' requires a boolean right operand");
    return context_.boolean();
  }
  if (binary.op == "??") {
    requireUnify(left, right, expr->range,
                 "nullish coalescing operands must unify to a single type");
    return left;
  }
  fail(expr->range, "unsupported operator '" + binary.op + "'");
}

TypePtr Checker::inferUpdate(const reader::ExprPtr &expr, const reader::UpdateExpr &update) {
  const TypePtr type = targetType(update.target);
  requireKind(type, TypeKind::Number, expr->range,
              "'" + update.op + "' requires a number target");
  return context_.number();
}

TypePtr Checker::inferAssignment(const reader::ExprPtr &expr,
                                 const reader::AssignmentExpr &assignment) {
  const TypePtr target = targetType(assignment.target);
  const TypePtr value = inferExpression(assignment.value);
  if (assignment.op == "=") {
    requireUnify(value, target, expr->range, "assigned value must unify with the target type");
    return target;
  }
  requireKind(target, TypeKind::Number, expr->range,
              "'" + assignment.op + "' requires a number target");
  requireKind(value, TypeKind::Number, assignment.value->range,
              "'" + assignment.op + "' requires a number value");
  return context_.number();
}

TypePtr Checker::targetType(const reader::ExprPtr &target) {
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&target->node)) {
    Binding *binding = lookupBinding(identifier->name);
    if (binding == nullptr) fail(target->range, "undefined identifier '" + identifier->name + "'");
    if (binding->isConst)
      fail(target->range, "cannot assign to constant binding '" + identifier->name + "'");
    return instantiate(binding->scheme, context_);
  }
  if (const auto *member = std::get_if<reader::MemberExpr>(&target->node)) {
    const TypePtr objectType = inferExpression(member->object);
    const MemberResolution resolution = resolveMember(objectType, *member);
    if (!resolution.valid)
      fail(target->range, "invalid assignment target");
    if (resolution.isStruct)
      annotations_.structFieldIndices.emplace(member, resolution.fieldIndex);
    return resolution.elementType;
  }
  fail(target->range, "invalid assignment target");
}

TypePtr Checker::inferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member) {
  const TypePtr objectType = inferExpression(member.object);
  const MemberResolution resolution = resolveMember(objectType, member);
  if (!resolution.valid) {
    const TypePtr resolved = context_.resolve(objectType);
    if (resolved != nullptr && resolved->kind == TypeKind::Array)
      fail(expr->range, "array element access requires a computed index");
    fail(expr->range, "member access is not supported on `" + context_.show(objectType) + "'");
  }
  if (resolution.isStruct)
    annotations_.structFieldIndices.emplace(&member, resolution.fieldIndex);
  return resolution.elementType;
}

Checker::MemberResolution Checker::resolveMember(TypePtr objectType,
                                                 const reader::MemberExpr &member) {
  MemberResolution resolution;
  const TypePtr resolved = context_.resolve(std::move(objectType));
  if (resolved == nullptr) return resolution;
  if (resolved->kind == TypeKind::Struct) {
    std::string key;
    if (!member.computed) {
      key = member.property;
    } else {
      const auto *literal =
          std::get_if<reader::StringLiteralExpr>(&member.computedProperty->node);
      if (literal == nullptr) return resolution;
      key = literal->value;
    }
    for (std::size_t i = 0; i < resolved->fields.size(); ++i) {
      if (resolved->fields[i].first == key) {
        resolution.valid = true;
        resolution.isStruct = true;
        resolution.elementType = resolved->fields[i].second;
        resolution.fieldIndex = i;
        return resolution;
      }
    }
    return resolution;
  }
  if (resolved->kind == TypeKind::Array) {
    if (!member.computed) return resolution;
    requireKind(inferExpression(member.computedProperty), TypeKind::Number,
                member.computedProperty->range, "array index must be a number");
    resolution.valid = true;
    resolution.isStruct = false;
    resolution.elementType = resolved->element;
    return resolution;
  }
  return resolution;
}

TypePtr Checker::inferCall(const reader::ExprPtr &expr, const reader::CallExpr &call) {
  const auto *identifier = std::get_if<reader::IdentifierExpr>(&call.callee->node);
  if (identifier != nullptr && lookupBinding(identifier->name) == nullptr) {
    const builtins::BuiltinSpec *builtin = builtins::BuiltinRegistry::instance().find(identifier->name);
    if (builtin != nullptr) {
      std::vector<TypePtr> arguments;
      for (const auto &argument : call.arguments) arguments.push_back(inferExpression(argument));
      if (!builtin->variadic && arguments.size() != builtin->minimumArguments)
        fail(expr->range, "'" + builtin->name + "' expects " +
                              std::to_string(builtin->minimumArguments) + " arguments, got " +
                              std::to_string(arguments.size()));
      if (builtin->variadic && arguments.size() < builtin->minimumArguments)
        fail(expr->range, "'" + builtin->name + "' expects at least " +
                              std::to_string(builtin->minimumArguments) + " arguments, got " +
                              std::to_string(arguments.size()));
      return builtin->infer(*this, arguments, expr->range);
    }
  }

  const TypePtr callee = inferExpression(call.callee);
  const TypePtr resolved = context_.resolve(callee);
  std::size_t arity = 0;
  if (resolved != nullptr && resolved->kind == TypeKind::Function) {
    arity = resolved->parameters.size();
    if (arity != call.arguments.size())
      fail(expr->range, "call target expects " + std::to_string(arity) + " arguments, got " +
                            std::to_string(call.arguments.size()));
  }

  TypePtr parameterType = call.arguments.empty() ? context_.variable()
                                                 : inferExpression(call.arguments.front());
  for (std::size_t i = 1; i < call.arguments.size(); ++i)
    requireUnify(inferExpression(call.arguments[i]), parameterType, expr->range,
                 "call arguments must unify to a single type");

  std::vector<TypePtr> parameters(arity == 0 && resolved != nullptr &&
                                          resolved->kind == TypeKind::Function
                                      ? arity
                                      : (call.arguments.empty() ? 1 : call.arguments.size()),
                                  parameterType);
  const TypePtr result = context_.variable();
  requireUnify(callee, context_.functionType(std::move(parameters), result), expr->range,
               "call target must be a function");
  return result;
}

TypePtr Checker::inferObject(const reader::ExprPtr &expr, const reader::ObjectExpr &object) {
  std::vector<std::pair<std::string, TypePtr>> fields;
  for (const auto &property : object.properties) {
    bool duplicate = false;
    for (const auto &field : fields)
      if (field.first == property.key) duplicate = true;
    if (duplicate)
      fail(expr->range, "duplicate property '" + property.key + "' in object literal");
    fields.emplace_back(property.key, inferExpression(property.value));
  }
  return context_.structType(std::move(fields));
}

TypePtr Checker::inferArray(const reader::ExprPtr &expr, const reader::ArrayExpr &array) {
  if (array.elements.empty()) return context_.arrayType(context_.variable());
  TypePtr element = inferExpression(array.elements.front());
  for (std::size_t i = 1; i < array.elements.size(); ++i)
    requireUnify(inferExpression(array.elements[i]), element, expr->range,
                 "array elements must unify to a single type");
  return context_.arrayType(element);
}

bool Checker::definitelyReturns(const reader::StatementList &statements) {
  for (const auto &statement : statements) {
    if (std::holds_alternative<reader::ReturnStatement>(statement.node)) return true;
    if (const auto *block = std::get_if<reader::BlockStatement>(&statement.node))
      if (definitelyReturns(block->statements)) return true;
    if (const auto *ifStatement = std::get_if<reader::IfStatement>(&statement.node))
      if (!ifStatement->consequent.empty() && !ifStatement->alternate.empty() &&
          definitelyReturns(ifStatement->consequent) &&
          definitelyReturns(ifStatement->alternate))
        return true;
  }
  return false;
}

Annotations Checker::check(const reader::Program &program) {
  annotations_ = Annotations{};
  std::vector<FunctionEntry> functions;
  hoistFunctions(program, functions);
  for (const auto &entry : functions) {
    inferFunctionBody(entry);
    const TypePtr type = context_.functionType(entry.parameters, entry.result);
    rootScope_.bindings.erase(entry.declaration->name);
    Binding binding;
    binding.scheme = generalize(type, environmentTypes(), context_);
    binding.isConst = true;
    rootScope_.bindings.insert_or_assign(entry.declaration->name, std::move(binding));
  }
  for (const auto &statement : program.statements) {
    if (std::holds_alternative<reader::FunctionDeclaration>(statement.node)) continue;
    inferStatement(statement);
  }
  return std::move(annotations_);
}

}
