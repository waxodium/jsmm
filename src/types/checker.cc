#include "types/checker.hh"

#include "errors/errors.hh"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace jsmm::types {
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

} // namespace

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
TypePtr Checker::resolveType(TypePtr type) { return context_.resolve(std::move(type)); }
std::string Checker::showType(TypePtr type) { return context_.show(std::move(type)); }
void Checker::report(const reader::SourceRange &range, const std::string &message) {
  fail(range, message);
}

void Checker::requireUnify(TypePtr left, TypePtr right, const reader::SourceRange &range,
                           const std::string &what) {
  const TypePtr leftCopy = left;
  const TypePtr rightCopy = right;
  if (!unifier_.unify(std::move(left), std::move(right)))
    fail(range, what + ": cannot unify `" + context_.show(leftCopy) + "' with `" +
                    context_.show(rightCopy) + "'");
}

void Checker::requireAssignable(TypePtr value, TypePtr target, const reader::SourceRange &range,
                                const std::string &what) {
  const TypePtr valueCopy = value;
  const TypePtr targetCopy = target;
  if (!unifier_.assignable(std::move(value), std::move(target)))
    fail(range, what + ": cannot unify `" + context_.show(valueCopy) + "' with `" +
                    context_.show(targetCopy) + "'");
}

void Checker::requireConcreteType(TypePtr type, const reader::SourceRange &range) {
  (void)range;
  pinType(type);
  concreteTypes_.push_back(std::move(type));
}

void Checker::deferCheck(std::function<void()> check) {
  deferredChecks_.push_back(std::move(check));
}

void Checker::runDeferredChecks() {
  for (const auto &check : deferredChecks_) check();
}

void Checker::pinType(TypePtr type) {
  std::vector<TypePtr> freeVariables;
  collectFreeVariables(std::move(type), freeVariables);
  for (const TypePtr &variable : freeVariables) pinnedTypes_.push_back(variable);
}

void Checker::collectFreeVariables(TypePtr type, std::vector<TypePtr> &out) {
  std::vector<TypePtr> worklist{std::move(type)};
  std::unordered_set<int> visited;
  while (!worklist.empty()) {
    TypePtr current = context_.resolve(worklist.back());
    worklist.pop_back();
    if (current == nullptr) continue;
    if (current->kind == TypeKind::Variable) {
      if (current->link == nullptr) out.push_back(current);
      continue;
    }
    if (!visited.insert(current->id).second) continue;
    switch (current->kind) {
    case TypeKind::Function:
      worklist.insert(worklist.end(), current->parameters.begin(), current->parameters.end());
      worklist.push_back(current->result);
      break;
    case TypeKind::Struct:
      for (const auto &field : current->fields) worklist.push_back(field.second);
      break;
    case TypeKind::Array: worklist.push_back(current->element); break;
    case TypeKind::Union:
      for (const auto &member : current->members) worklist.push_back(member);
      break;
    default: break;
    }
  }
}

void Checker::collectQuantifiedVariables(std::set<int> &out) const {
  for (const auto &scope : scopePool_)
    for (const auto &entry : scope->bindings)
      for (const int id : entry.second.scheme.variables) out.insert(id);
  for (const auto &entry : rootScope_.bindings)
    for (const int id : entry.second.scheme.variables) out.insert(id);
}

Checker::Scope *Checker::pushScope(Scope *parent) {
  scopePool_.push_back(std::make_unique<Scope>());
  Scope *scope = scopePool_.back().get();
  scope->parent = parent;
  currentScope_ = scope;
  return scope;
}

void Checker::popScope() { currentScope_ = currentScope_->parent; }

Checker::Binding *Checker::lookupBinding(const std::string &name) {
  for (Scope *scope = currentScope_; scope != nullptr; scope = scope->parent) {
    const auto found = scope->bindings.find(name);
    if (found != scope->bindings.end()) return &found->second;
  }
  return nullptr;
}

Checker::Scope *Checker::lookupVarScope() {
  for (Scope *scope = currentScope_; scope != nullptr; scope = scope->parent) {
    if (scope->isFunction || scope->parent == nullptr) return scope;
  }
  return currentScope_;
}

std::vector<TypePtr> Checker::environmentTypes() const {
  std::vector<TypePtr> types;
  for (const Scope *scope = currentScope_; scope != nullptr; scope = scope->parent)
    for (const auto &[name, binding] : scope->bindings) types.push_back(binding.scheme.type);
  return types;
}

[[noreturn]] void Checker::fail(const reader::SourceRange &range,
                                const std::string &message) const {
  fail(range, message, errors::ErrorCode::TYPE_CANNOT_UNIFY);
}

[[noreturn]] void Checker::fail(const reader::SourceRange &range, const std::string &message,
                                errors::ErrorCode code) const {
  throw errors::ErrorException{errors::ErrorStatus{code, message, range}};
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
    requireAssignable(std::move(type), std::move(target), range, what);
    return;
  }
  type = context_.resolve(std::move(type));
  if (type == nullptr || type->kind != kind)
    fail(range,
         what + ": expected `" + std::string(kindName(kind)) + "', got `" + context_.show(type) +
             "'",
         errors::ErrorCode::TYPE_EXPECTED_KIND);
}

TypePtr Checker::annotate(const reader::ExprPtr &expr, TypePtr type) {
  annotations_.expressionTypes.emplace(expr.get(), type);
  return type;
}

bool Checker::isNullish(TypePtr type) {
  if (type == nullptr) return false;
  return type->kind == TypeKind::Null || type->kind == TypeKind::Undefined;
}

TypePtr Checker::unionPayload(TypePtr type) {
  if (type == nullptr || type->kind != TypeKind::Union) return nullptr;
  for (const TypePtr &member : type->members)
    if (!isNullish(member)) return member;
  return nullptr;
}

TypePtr Checker::resolveAnnotation(const reader::TypeExpr &annotation,
                                   const reader::SourceRange &range) {
  std::vector<TypePtr> members;
  for (const std::string &name : annotation.names) {
    TypePtr member;
    if (name == "number") member = context_.number();
    else if (name == "string") member = context_.string();
    else if (name == "boolean") member = context_.boolean();
    else if (name == "null") member = context_.null();
    else if (name == "undefined") member = context_.undefined();
    else
      fail(range, "unknown type name '" + name + "'", errors::ErrorCode::TYPE_UNKNOWN_TYPE_NAME);
    bool duplicate = false;
    for (const TypePtr &existing : members)
      if (existing->kind == member->kind) duplicate = true;
    if (!duplicate) members.push_back(std::move(member));
  }
  if (members.empty())
    fail(range, "a type annotation must name at least one type",
         errors::ErrorCode::PARSE_EXPECTED_TYPE_NAME);
  if (members.size() == 1) return members.front();
  std::size_t payloads = 0;
  for (const TypePtr &member : members)
    if (!isNullish(member)) ++payloads;
  if (payloads > 1)
    fail(range, "a union may hold at most one non-nullable member",
         errors::ErrorCode::TYPE_UNION_REQUIRES_SINGLE_PAYLOAD);
  return context_.unionType(std::move(members));
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

void Checker::inferFunctionBody(const FunctionEntry &entry, Scope *parentScope) {
  Scope *funcScope = pushScope(parentScope);
  funcScope->isFunction = true;
  for (std::size_t i = 0; i < entry.declaration->parameters.size(); ++i) {
    Binding binding;
    binding.scheme = TypeScheme{{}, entry.parameters[i]};
    binding.isConst = true;
    currentScope_->bindings.emplace(entry.declaration->parameters[i], std::move(binding));
  }
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

void Checker::inferDeclaration(const reader::VariableDeclaration &declaration) {
  for (const auto &declarator : declaration.declarations) {
    const bool initialized = declarator.initializer != nullptr;
    TypePtr type;
    if (declarator.annotation != nullptr) {
      type = resolveAnnotation(*declarator.annotation, declarator.annotation->range);
      annotations_.declaredTypes.emplace(&declarator, context_.resolve(type));
      if (initialized) {
        const TypePtr valueType = inferExpression(declarator.initializer);
        requireAssignable(valueType, type, declarator.initializer->range,
                          "assigned value must unify with the declared type");
      }
    } else {
      type = initialized ? inferExpression(declarator.initializer) : context_.variable();
    }
    Binding binding;
    binding.scheme =
        initialized ? generalize(type, environmentTypes(), context_) : TypeScheme{{}, type};
    binding.isConst = declaration.kind == "const";
    Scope *target = (declaration.kind == "var") ? lookupVarScope() : currentScope_;
    target->bindings.insert_or_assign(declarator.name, std::move(binding));
    if (!initialized && declarator.annotation == nullptr)
      declarationTypeVars_.emplace(&declarator, type);
  }
}

void Checker::inferStatement(const reader::Statement &statement) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          inferDeclaration(node);
        } else if constexpr (std::is_same_v<T, reader::ExpressionStatement>) {
          (void)inferExpression(node.expression);
        } else if constexpr (std::is_same_v<T, reader::FunctionDeclaration>) {
          if (inFunction_)
            fail(statement.range, "function declarations must appear at the top level",
                 errors::ErrorCode::TYPE_FUNCTION_DECLARATIONS_MUST_BE_TOP_LEVEL);
        } else if constexpr (std::is_same_v<T, reader::ReturnStatement>) {
          if (returnTargets_.empty())
            fail(statement.range, "'return' outside of a function body",
                 errors::ErrorCode::TYPE_RETURN_OUTSIDE_FUNCTION);
          const TypePtr type =
              node.argument != nullptr ? inferExpression(node.argument) : context_.undefined();
          requireAssignable(type, returnTargets_.back(), statement.range, "return value");
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
          if (node.init != nullptr) inferDeclaration(*node.init);
          if (node.condition != nullptr)
            requireKind(inferExpression(node.condition), TypeKind::Boolean, node.condition->range,
                        "for condition must be boolean");
          if (node.update != nullptr) (void)inferExpression(node.update);
          ++loopDepth_;
          inferStatements(node.body);
          --loopDepth_;
          popScope();
        } else if constexpr (std::is_same_v<T, reader::BreakStatement>) {
          if (loopDepth_ == 0)
            fail(statement.range, "'break' outside of a loop",
                 errors::ErrorCode::TYPE_BREAK_OUTSIDE_LOOP);
        } else if constexpr (std::is_same_v<T, reader::ContinueStatement>) {
          if (loopDepth_ == 0)
            fail(statement.range, "'continue' outside of a loop",
                 errors::ErrorCode::TYPE_CONTINUE_OUTSIDE_LOOP);
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
    fail(expr->range, "builtin '" + identifier.name + "' cannot be used as a value",
         errors::ErrorCode::TYPE_BUILTIN_CANNOT_BE_USED_AS_VALUE);
  fail(expr->range, "undefined identifier '" + identifier.name + "'",
       errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER);
}

TypePtr Checker::inferUnary(const reader::ExprPtr &expr, const reader::UnaryExpr &unary) {
  if (unary.op == "typeof") {
    const TypePtr operand = inferExpression(unary.operand);
    requireConcreteType(operand, unary.operand->range);
    return context_.string();
  }
  if (unary.op == "void") {
    (void)inferExpression(unary.operand);
    return context_.undefined();
  }
  if (unary.op == "delete")
    fail(expr->range, "'delete' is not supported in the static subset",
         errors::ErrorCode::TYPE_DELETE_NOT_SUPPORTED);
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
      const TypePtr leftResolved = context_.resolve(left);
      const TypePtr rightResolved = context_.resolve(right);
      const bool leftUnion =
          leftResolved != nullptr && leftResolved->kind == TypeKind::Union;
      const bool rightUnion =
          rightResolved != nullptr && rightResolved->kind == TypeKind::Union;
      if (leftUnion || rightUnion) {
        if (leftUnion && rightUnion) {
          requireUnify(left, right, expr->range, "equality operands must have the same type");
          return context_.boolean();
        }
        const TypePtr other = leftUnion ? rightResolved : leftResolved;
        if (!isNullish(other))
          fail(expr->range,
               "a nullable value may only be compared with null or undefined",
               errors::ErrorCode::TYPE_NULLABLE_COMPARISON_REQUIRES_NULLISH);
        return context_.boolean();
      }
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
    const TypePtr leftResolved = context_.resolve(left);
    const TypePtr rightResolved = context_.resolve(right);
    if (isNullish(leftResolved)) return right;
    if (leftResolved != nullptr && leftResolved->kind == TypeKind::Union) {
      const TypePtr payload = unionPayload(leftResolved);
      if (payload == nullptr) return right;
      requireAssignable(right, payload, expr->range,
                       "nullish coalescing right operand must unify with the payload type");
      return payload;
    }
    if (leftResolved != nullptr && leftResolved->kind == TypeKind::Variable &&
        rightResolved != nullptr && rightResolved->kind != TypeKind::Variable &&
        !isNullish(rightResolved)) {
      std::vector<TypePtr> members;
      members.push_back(rightResolved);
      members.push_back(context_.null());
      const TypePtr nullable = context_.unionType(std::move(members));
      unifier_.unify(left, nullable);
      return rightResolved;
    }
    requireUnify(left, right, expr->range,
                 "nullish coalescing operands must unify to a single type");
    return left;
  }
  fail(expr->range, "unsupported operator '" + binary.op + "'",
       errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR);
}

TypePtr Checker::inferUpdate(const reader::ExprPtr &expr, const reader::UpdateExpr &update) {
  const TypePtr type = targetType(update.target);
  requireKind(type, TypeKind::Number, expr->range, "'" + update.op + "' requires a number target");
  return context_.number();
}

TypePtr Checker::inferAssignment(const reader::ExprPtr &expr,
                                 const reader::AssignmentExpr &assignment) {
  const TypePtr target = targetType(assignment.target);
  annotations_.expressionTypes.insert_or_assign(assignment.target.get(), target);
  const TypePtr value = inferExpression(assignment.value);
  if (assignment.op == "=") {
    requireAssignable(value, target, expr->range,
                       "assigned value must unify with the target type");
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
    if (binding == nullptr)
      fail(target->range, "undefined identifier '" + identifier->name + "'",
           errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER);
    if (binding->isConst)
      fail(target->range, "cannot assign to constant binding '" + identifier->name + "'",
           errors::ErrorCode::TYPE_CANNOT_ASSIGN_TO_CONST);
    return instantiate(binding->scheme, context_);
  }
  if (const auto *member = std::get_if<reader::MemberExpr>(&target->node)) {
    const TypePtr objectType = inferExpression(member->object);
    const MemberResolution resolution = resolveMember(objectType, *member);
    if (!resolution.valid) {
      const TypePtr resolved = context_.resolve(objectType);
      if (resolved != nullptr && resolved->kind == TypeKind::Variable) {
        const TypePtr resultType = context_.variable();
        deferMember(target, *member, objectType, resultType);
        return resultType;
      }
      fail(target->range, "invalid assignment target",
           errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET);
    }
    if (resolution.isStruct) annotations_.structFieldIndices.emplace(member, resolution.fieldIndex);
    return resolution.elementType;
  }
  fail(target->range, "invalid assignment target",
       errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET);
}

TypePtr Checker::inferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member) {
  const TypePtr objectType = inferExpression(member.object);
  const MemberResolution resolution = resolveMember(objectType, member);
  if (!resolution.valid) {
    const TypePtr resolved = context_.resolve(objectType);
    if (resolved != nullptr && resolved->kind == TypeKind::Array)
      fail(expr->range, "array element access requires a computed index",
           errors::ErrorCode::TYPE_ARRAY_ELEMENT_ACCESS_REQUIRES_COMPUTED_INDEX);
    if (resolved != nullptr && resolved->kind == TypeKind::Variable) {
      const TypePtr resultType = context_.variable();
      deferMember(expr, member, objectType, resultType);
      return resultType;
    }
    fail(expr->range, "member access is not supported on `" + context_.show(objectType) + "'",
         errors::ErrorCode::TYPE_MEMBER_ACCESS_NOT_SUPPORTED);
  }
  if (resolution.isStruct) annotations_.structFieldIndices.emplace(&member, resolution.fieldIndex);
  return resolution.elementType;
}

void Checker::deferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member,
                          TypePtr objectType, TypePtr resultType) {
  PendingMember pending;
  pending.expression = expr.get();
  pending.member = &member;
  pending.objectType = std::move(objectType);
  pending.resultType = std::move(resultType);
  pending.range = expr->range;
  pendingMembers_.push_back(std::move(pending));
  memberResults_.push_back(pendingMembers_.back().resultType);
  requireConcreteType(pendingMembers_.back().objectType, pending.range);
  pinType(pendingMembers_.back().resultType);
}

void Checker::resolvePendingMembers() {
  for (std::size_t index = 0; index < pendingMembers_.size(); ++index) {
    const PendingMember pending = pendingMembers_[index];
    const MemberResolution resolution = resolveMember(pending.objectType, *pending.member);
    if (!resolution.valid) {
      const TypePtr resolved = context_.resolve(pending.objectType);
      if (resolved != nullptr && resolved->kind == TypeKind::Array)
        fail(pending.range, "array element access requires a computed index",
             errors::ErrorCode::TYPE_ARRAY_ELEMENT_ACCESS_REQUIRES_COMPUTED_INDEX);
      fail(pending.range,
           "member access is not supported on `" + context_.show(pending.objectType) + "'",
           errors::ErrorCode::TYPE_MEMBER_ACCESS_NOT_SUPPORTED);
    }
    requireAssignable(resolution.elementType, pending.resultType, pending.range,
                       "member access type");
    if (resolution.isStruct)
      annotations_.structFieldIndices.emplace(pending.member, resolution.fieldIndex);
    annotations_.expressionTypes.insert_or_assign(pending.expression,
                                                  context_.resolve(pending.resultType));
  }
}

void Checker::bindUnresolvedTypes(bool excludeMemberResults) {
  std::set<int> quantified;
  collectQuantifiedVariables(quantified);
  std::set<int> excluded;
  if (excludeMemberResults) {
    for (const TypePtr &result : memberResults_) {
      std::vector<TypePtr> freeVariables;
      collectFreeVariables(result, freeVariables);
      for (const TypePtr &variable : freeVariables) excluded.insert(variable->id);
    }
  }
  for (const TypePtr &type : concreteTypes_) {
    std::vector<TypePtr> freeVariables;
    collectFreeVariables(type, freeVariables);
    for (const TypePtr &variable : freeVariables) {
      if (quantified.contains(variable->id)) continue;
      if (excluded.contains(variable->id)) continue;
      requireAssignable(context_.undefined(), variable, reader::SourceRange{},
                         "unresolved type defaults to undefined");
    }
  }
}

void Checker::recordTopLevelTypes() {
  for (const auto &entry : rootScope_.bindings)
    annotations_.topLevelTypes.emplace(entry.first, context_.resolve(entry.second.scheme.type));
  for (const auto &entry : declarationTypeVars_)
    annotations_.declarationTypes.emplace(entry.first, context_.resolve(entry.second));
}

void Checker::finalize() {
  bindUnresolvedTypes(true);
  resolvePendingMembers();
  bindUnresolvedTypes(false);
  runDeferredChecks();
  recordTopLevelTypes();
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
      const auto *literal = std::get_if<reader::StringLiteralExpr>(&member.computedProperty->node);
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
    const builtins::BuiltinSpec *builtin =
        builtins::BuiltinRegistry::instance().find(identifier->name);
    if (builtin != nullptr) {
      std::vector<TypePtr> arguments;
      for (const auto &argument : call.arguments) arguments.push_back(inferExpression(argument));
      if (!builtin->variadic && arguments.size() != builtin->minimumArguments)
        fail(expr->range,
             "'" + builtin->name + "' expects " + std::to_string(builtin->minimumArguments) +
                 " arguments, got " + std::to_string(arguments.size()),
             errors::ErrorCode::TYPE_BUILTIN_EXPECTS_ARGUMENTS);
      if (builtin->variadic && arguments.size() < builtin->minimumArguments)
        fail(expr->range,
             "'" + builtin->name + "' expects at least " +
                 std::to_string(builtin->minimumArguments) + " arguments, got " +
                 std::to_string(arguments.size()),
             errors::ErrorCode::TYPE_BUILTIN_EXPECTS_AT_LEAST_ARGUMENTS);
      return builtin->infer(*this, arguments, expr->range);
    }
  }

  const TypePtr callee = inferExpression(call.callee);
  const TypePtr resolved = context_.resolve(callee);
  std::size_t arity = 0;
  if (resolved != nullptr && resolved->kind == TypeKind::Function) {
    arity = resolved->parameters.size();
    if (arity != call.arguments.size())
      fail(expr->range,
           "call target expects " + std::to_string(arity) + " arguments, got " +
               std::to_string(call.arguments.size()),
           errors::ErrorCode::TYPE_CALL_TARGET_EXPECTS_ARGUMENTS);
  }

  std::vector<TypePtr> parameters;
  parameters.reserve(call.arguments.size());
  for (const auto &argument : call.arguments) parameters.push_back(inferExpression(argument));
  const TypePtr result = context_.variable();
  const bool calleeIsFunction = resolved != nullptr && resolved->kind == TypeKind::Function;
  if (calleeIsFunction) {
    for (std::size_t i = 0; i < parameters.size(); ++i) {
      requireAssignable(parameters[i], resolved->parameters[i], expr->range,
                        "call arguments do not match the parameter types");
      annotations_.argumentTypes.emplace(call.arguments[i].get(),
                                         context_.resolve(resolved->parameters[i]));
    }
    requireUnify(resolved->result, result, expr->range, "call result type");
    return result;
  }
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
      fail(expr->range, "duplicate property '" + property.key + "' in object literal",
           errors::ErrorCode::TYPE_DUPLICATE_PROPERTY);
    fields.emplace_back(property.key, inferExpression(property.value));
  }
  return context_.structType(std::move(fields));
}

TypePtr Checker::inferArray(const reader::ExprPtr &expr, const reader::ArrayExpr &array) {
  if (array.elements.empty()) return context_.arrayType(context_.variable());
  TypePtr element = inferExpression(array.elements.front());
  for (std::size_t i = 1; i < array.elements.size(); ++i) {
    const TypePtr current = inferExpression(array.elements[i]);
    const TypePtr elementResolved = context_.resolve(element);
    const TypePtr currentResolved = context_.resolve(current);
    const bool elementUnion =
        elementResolved != nullptr && elementResolved->kind == TypeKind::Union;
    const bool currentUnion = currentResolved != nullptr && currentResolved->kind == TypeKind::Union;
    if (elementUnion && !currentUnion) {
      requireAssignable(current, element, expr->range,
                        "array elements must unify to a single type");
    } else if (!elementUnion && currentUnion) {
      requireAssignable(element, current, expr->range,
                        "array elements must unify to a single type");
      element = current;
    } else {
      requireUnify(current, element, expr->range, "array elements must unify to a single type");
    }
  }
  return context_.arrayType(element);
}

bool Checker::definitelyReturns(const reader::StatementList &statements) {
  for (const auto &statement : statements) {
    if (std::holds_alternative<reader::ReturnStatement>(statement.node)) return true;
    if (const auto *block = std::get_if<reader::BlockStatement>(&statement.node))
      if (definitelyReturns(block->statements)) return true;
    if (const auto *ifStatement = std::get_if<reader::IfStatement>(&statement.node))
      if (!ifStatement->consequent.empty() && !ifStatement->alternate.empty() &&
          definitelyReturns(ifStatement->consequent) && definitelyReturns(ifStatement->alternate))
        return true;
  }
  return false;
}

Annotations Checker::check(const reader::Program &program) {
  annotations_ = Annotations{};
  std::vector<FunctionEntry> functions;
  hoistFunctions(program, functions);

  for (const auto &statement : program.statements) {
    if (std::holds_alternative<reader::VariableDeclaration>(statement.node)) {
      inferStatement(statement);
    }
  }

  for (const auto &entry : functions) {
    pinnedTypes_.clear();
    inferFunctionBody(entry, &rootScope_);
    const TypePtr type = context_.functionType(entry.parameters, entry.result);
    rootScope_.bindings.erase(entry.declaration->name);
    Binding binding;
    std::vector<TypePtr> environment = environmentTypes();
    for (const TypePtr &pinned : pinnedTypes_) environment.push_back(pinned);
    binding.scheme = generalize(type, environment, context_);
    binding.isConst = true;
    rootScope_.bindings.insert_or_assign(entry.declaration->name, std::move(binding));
  }

  for (const auto &statement : program.statements) {
    if (std::holds_alternative<reader::VariableDeclaration>(statement.node) ||
        std::holds_alternative<reader::FunctionDeclaration>(statement.node))
      continue;
    inferStatement(statement);
  }

  finalize();
  return std::move(annotations_);
}

} // namespace jsmm::types
