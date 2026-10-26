#include "types/checker.hh"

#include "errors/errors.hh"

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <utility>

namespace jsmm::types {

TypePtr Checker::unionPayload(TypePtr type) const {
  type = resolve(std::move(type));
  if (type == nullptr || type->kind != TypeKind::Union) return nullptr;
  for (const TypePtr &member : type->members) {
    const TypePtr resolved = resolve(member);
    if (resolved == nullptr) continue;
    if (resolved->kind == TypeKind::Null || resolved->kind == TypeKind::Undefined) continue;
    return resolved;
  }
  return nullptr;
}

bool Checker::isNumber(TypePtr type) const {
  type = resolve(std::move(type));
  return type != nullptr && type->kind == TypeKind::Number;
}

Annotations Checker::check(const reader::Program &program) {
  scopes_.push_back(Scope{});
  hoistFunctions(program, functions_);
  hoistVarDeclarations(program.statements, &scopes_.back());
  inferStatements(program.statements);
  recordTopLevelTypes();
  finalize();
  scopes_.pop_back();
  return annotations_;
}

void Checker::hoistFunctions(const reader::Program &program, std::vector<FunctionEntry> &functions) {
  for (const auto &statement : program.statements) {
    if (const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement->node)) {
      auto bodyBlock = std::make_unique<reader::BlockStatement>();
      bodyBlock->statements = std::move(const_cast<reader::BlockStatement &>(*declaration->body).statements);
      functions.emplace_back(FunctionEntry{reader::FunctionDeclaration{declaration->name, declaration->parameters, std::move(bodyBlock)}, nullptr, &scopes_.back()});
    }
  }
}

void Checker::hoistVarDeclarations(const reader::StatementList &statements, Scope *scope) {
  for (const auto &statement : statements) hoistVarDeclarations(statement, scope);
}

void Checker::hoistVarDeclarations(const std::unique_ptr<reader::Statement> &stmt, Scope *scope) {
  if (stmt == nullptr) return;
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          for (const auto &declarator : node.declarations)
            scope->bindings.insert_or_assign(declarator.name, nullptr);
        } else if constexpr (std::is_same_v<T, reader::BlockStatement>) {
          scopes_.push_back(Scope{});
          hoistVarDeclarations(node.statements, &scopes_.back());
          scopes_.pop_back();
        } else if constexpr (std::is_same_v<T, reader::IfStatement>) {
          hoistVarDeclarations(node.consequent, scope);
          hoistVarDeclarations(node.alternate, scope);
        } else if constexpr (std::is_same_v<T, reader::WhileStatement>) {
          hoistVarDeclarations(node.body, scope);
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          if (node.init != nullptr && node.init->kind == "var") {
            for (const auto &declarator : node.init->declarations)
              scope->bindings.insert_or_assign(declarator.name, nullptr);
          }
          hoistVarDeclarations(node.body, scope);
        } else if constexpr (std::is_same_v<T, reader::LabeledStatement>) {
          if (node.body != nullptr) hoistVarDeclarations(node.body, scope);
        }
      },
      stmt->node);
}

void Checker::inferFunctionBody(FunctionEntry &entry, Scope *parentScope) {
  (void)parentScope;
  Scope scope;
  for (const auto &parameter : entry.declaration.parameters)
    scope.bindings.insert_or_assign(parameter.name, nullptr);
  scopes_.push_back(std::move(scope));
  for (const auto &stmt : entry.declaration.body->statements)
    inferStatement(*stmt);
  scopes_.pop_back();
  entry.returnType = undefinedType();
  if (definitelyReturns(entry.declaration.body->statements))
    entry.returnType = undefinedType();
}

void Checker::inferStatements(const reader::StatementList &statements) {
  for (const auto &statement : statements) inferStatement(*statement);
}

void Checker::inferDeclaration(const reader::VariableDeclaration &declaration) {
  for (auto &declarator : declaration.declarations) {
    TypePtr initializerType;
    if (declarator.initializer != nullptr) {
      initializerType = inferExpression(declarator.initializer);
      annotations_.declarationTypes.insert_or_assign(&declarator, initializerType);
    }
    const auto existing = scopes_.back().bindings.find(declarator.name);
    if (existing != scopes_.back().bindings.end() && existing->second == nullptr) {
      scopes_.back().bindings[declarator.name] = initializerType;
    } else if (initializerType != nullptr) {
      if (!unifies(existing == scopes_.back().bindings.end() ? nullptr : existing->second,
                    initializerType, "assignment"))
        throw errors::ErrorException{
            errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, declarator.name, declarator.range}};
      if (existing == scopes_.back().bindings.end()) {
        scopes_.back().bindings.insert_or_assign(declarator.name, initializerType);
      } else {
        existing->second = initializerType;
      }
    }
  }
}

void Checker::inferStatement(const reader::Statement &statement) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          inferDeclaration(node);
        } else if constexpr (std::is_same_v<T, reader::ExpressionStatement>) {
          if (node.expression != nullptr) (void)inferExpression(node.expression);
        } else if constexpr (std::is_same_v<T, reader::FunctionDeclaration>) {
          for (auto &function : functions_)
            if (function.declaration.name == node.name) {
              inferFunctionBody(function, &scopes_.back());
              break;
            }
        } else if constexpr (std::is_same_v<T, reader::ReturnStatement>) {
          const auto function = std::find_if(
              functions_.begin(), functions_.end(),
              [&](const FunctionEntry &entry) { return entry.parentScope == &scopes_.back(); });
          if (function != functions_.end()) {
            if (node.argument != nullptr) {
              const TypePtr argument = inferExpression(node.argument);
              if (!unifies(function->returnType, argument, "return"))
                throw errors::ErrorException{
                    errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, "return", statement.range}};
            } else {
              if (!unifies(function->returnType, undefinedType(), "return"))
                throw errors::ErrorException{
                    errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, "return", statement.range}};
            }
          } else {
            throw errors::ErrorException{
                errors::ErrorStatus{errors::ErrorCode::TYPE_RETURN_OUTSIDE_FUNCTION, "return", statement.range}};
          }
        } else if constexpr (std::is_same_v<T, reader::BlockStatement>) {
          scopes_.push_back(Scope{});
          inferStatements(node.statements);
          scopes_.pop_back();
        } else if constexpr (std::is_same_v<T, reader::IfStatement>) {
          (void)inferExpression(node.condition);
          inferStatements(node.consequent);
          inferStatements(node.alternate);
        } else if constexpr (std::is_same_v<T, reader::WhileStatement>) {
          (void)inferExpression(node.condition);
          inferStatements(node.body);
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          if (node.init != nullptr) inferDeclaration(*node.init);
          if (node.initExpression != nullptr) (void)inferExpression(node.initExpression);
          if (node.condition != nullptr) (void)inferExpression(node.condition);
          if (node.update != nullptr) (void)inferExpression(node.update);
          inferStatements(node.body);
        } else if constexpr (std::is_same_v<T, reader::BreakStatement>) {
          (void)node.label;
        } else if constexpr (std::is_same_v<T, reader::ContinueStatement>) {
          (void)node.label;
        } else if constexpr (std::is_same_v<T, reader::ThrowStatement>) {
          (void)inferExpression(node.argument);
        } else if constexpr (std::is_same_v<T, reader::LabeledStatement>) {
          if (node.body != nullptr) inferStatement(*node.body);
        }
      },
      statement.node);
}

TypePtr Checker::inferExpression(const reader::ExprPtr &expr) {
  const TypePtr type = std::visit(
      [&](const auto &node) -> TypePtr {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::NumberLiteralExpr>) {
          return numberType();
        } else if constexpr (std::is_same_v<T, reader::StringLiteralExpr>) {
          return stringType();
        } else if constexpr (std::is_same_v<T, reader::IdentifierExpr>) {
          return inferIdentifier(expr, node);
        } else if constexpr (std::is_same_v<T, reader::CallExpr>) {
          return inferCall(expr, node);
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
        } else if constexpr (std::is_same_v<T, reader::BooleanLiteralExpr>) {
          return booleanType();
        } else if constexpr (std::is_same_v<T, reader::NullLiteralExpr>) {
          return nullType();
        } else if constexpr (std::is_same_v<T, reader::UndefinedLiteralExpr>) {
          return undefinedType();
        }
        return neverType();
      },
      expr->node);
  annotations_.expressionTypes.insert_or_assign(expr.get(), type);
  return type;
}

TypePtr Checker::inferIdentifier(const reader::ExprPtr &expr, const reader::IdentifierExpr &identifier) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->bindings.find(identifier.name);
    if (found != scope->bindings.end()) {
      if (found->second != nullptr) return found->second;
    }
  }
  for (const auto &function : functions_) {
    if (function.declaration.name == identifier.name) {
      std::vector<TypePtr> parameterTypes;
      parameterTypes.reserve(function.declaration.parameters.size());
      (void)function.declaration.parameters;
      TypePtr calleeType = this->makeFunctionType(undefinedType(), std::move(parameterTypes));
      annotations_.argumentTypes.insert_or_assign(expr.get(), calleeType);
      return calleeType;
    }
  }
  annotations_.argumentTypes.insert_or_assign(expr.get(), nullptr);
  return nullptr;
}

TypePtr Checker::inferUnary(const reader::ExprPtr &expr, const reader::UnaryExpr &unary) {
  if (unary.op == "!") {
    const TypePtr operand = inferExpression(unary.operand);
    annotations_.argumentTypes.insert_or_assign(expr.get(), operand);
    return booleanType();
  }
  if (unary.op == "~" || unary.op == "-") {
    const TypePtr operand = inferExpression(unary.operand);
    if (!isNumber(operand))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, unary.op, expr->range}};
    annotations_.argumentTypes.insert_or_assign(expr.get(), operand);
    return numberType();
  }
  if (unary.op == "+") {
    (void)inferExpression(unary.operand);
    return undefinedType();
  }
  if (unary.op == "typeof") {
    (void)inferExpression(unary.operand);
    return stringType();
  }
  if (unary.op == "void") {
    (void)inferExpression(unary.operand);
    return undefinedType();
  }
  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, unary.op, expr->range}};
}

TypePtr Checker::inferBinary(const reader::ExprPtr &expr, const reader::BinaryExpr &binary) {
  const TypePtr left = inferExpression(binary.left);
  const TypePtr right = inferExpression(binary.right);
  annotations_.argumentTypes.insert_or_assign(expr.get(), left);

  if (binary.op == "||" || binary.op == "&&" || binary.op == "??") {
    if (!unifies(left, right, binary.op))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNION_REQUIRES_SINGLE_PAYLOAD, binary.op, expr->range}};
    if (binary.op == "??") { return left; }
    return left;
  }

  if (binary.op == "==" || binary.op == "!=" || binary.op == "===" || binary.op == "!==") {
    if (!unifies(left, right, binary.op))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNION_REQUIRES_SINGLE_PAYLOAD, binary.op, expr->range}};
    return booleanType();
  }

  if (binary.op == "+" || binary.op == "-" || binary.op == "*" || binary.op == "/" ||
      binary.op == "%" || binary.op == "**") {
    if (!isNumber(left) || !isNumber(right))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, binary.op, expr->range}};
    return numberType();
  }

  if (binary.op == "<" || binary.op == ">" || binary.op == "<=" || binary.op == ">=") {
    if (!isNumber(left) || !isNumber(right))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, binary.op, expr->range}};
    return booleanType();
  }

  if (binary.op == "&" || binary.op == "|" || binary.op == "^" || binary.op == "<<" ||
      binary.op == ">>" || binary.op == ">>>") {
    if (!isNumber(left) || !isNumber(right))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, binary.op, expr->range}};
    return numberType();
  }

  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, binary.op, expr->range}};
}

TypePtr Checker::inferUpdate(const reader::ExprPtr &expr, const reader::UpdateExpr &update) {
  TypePtr target = targetType(update.operand);
  if (!isNumber(target))
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, update.op, expr->range}};
  annotations_.argumentTypes.insert_or_assign(expr.get(), target);
  return numberType();
}

TypePtr Checker::inferAssignment(const reader::ExprPtr &expr, const reader::AssignmentExpr &assignment) {
  TypePtr right = inferExpression(assignment.right);
  TypePtr target = targetType(assignment.left);
  if (!unifies(target, right, "assignment"))
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, "assignment", expr->range}};
  annotations_.argumentTypes.insert_or_assign(expr.get(), right);
  return right;
}

TypePtr Checker::targetType(const reader::ExprPtr &target) {
  const TypePtr type = inferExpression(target);
  if (type == nullptr || type->kind == TypeKind::Variable)
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, "assignment", target->range}};
  return type;
}

TypePtr Checker::inferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member) {
  TypePtr objectType = inferExpression(member.object);
  if (member.computedFlag) {
    TypePtr indexType = inferExpression(member.computed);
    if (!isNumber(indexType))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "[]", expr->range}};
    annotations_.argumentTypes.insert_or_assign(expr.get(), objectType);
    return objectType;
  }
  annotations_.argumentTypes.insert_or_assign(expr.get(), objectType);
  return objectType;
}

void Checker::deferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member,
                          TypePtr objectType, TypePtr result) {
  (void)objectType;
  pendingMembers_.push_back(std::make_pair(expr.get(), member));
  pendingTypeResolutions_.insert_or_assign(expr.get(), result);
}

void Checker::resolvePendingMembers() {
  (void)pendingMembers_;
  (void)pendingTypeResolutions_;
}

void Checker::bindUnresolvedTypes(bool excludeMemberResults) {
  (void)excludeMemberResults;
}

void Checker::recordTopLevelTypes() {
  (void)pendingMembers_;
  (void)pendingTypeResolutions_;
}

void Checker::finalize() {
  resolvePendingMembers();
  bindUnresolvedTypes(false);
  for (auto it = annotations_.expressionTypes.begin(); it != annotations_.expressionTypes.end();) {
    if (it->second == nullptr) {
      it = annotations_.expressionTypes.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = annotations_.declarationTypes.begin(); it != annotations_.declarationTypes.end();) {
    if (it->second == nullptr) {
      it = annotations_.declarationTypes.erase(it);
    } else {
      ++it;
    }
  }
}

bool Checker::unifies(TypePtr left, TypePtr right, std::string_view context) const {
  if (left == nullptr || right == nullptr) return true;
  left = resolve(left);
  right = resolve(right);
  if (left == right) return true;
  if (left == nullptr || right == nullptr) return true;
  if (left->kind == TypeKind::Union && right->kind == TypeKind::Union) { return true; }
  if (left->kind == TypeKind::Union) {
    for (const auto &member : left->members)
      if (!unifies(member, right, context)) return false;
    return true;
  }
  if (right->kind == TypeKind::Union) {
    for (const auto &member : right->members)
      if (!unifies(left, member, context)) return false;
    return true;
  }
  if (left->kind != right->kind) return false;
  if (left->kind == TypeKind::Struct) {
    if (left->fields.size() != right->fields.size()) return false;
    for (std::size_t i = 0; i < left->fields.size(); ++i)
      if (!unifies(left->fields[i].second, right->fields[i].second, context)) return false;
    return true;
  }
  return true;
}

bool Checker::definitelyReturns(const reader::StatementList &statements) const {
  for (const auto &statement : statements) {
    if (std::get_if<reader::BlockStatement>(&statement->node)) {
      if (definitelyReturns(std::get<reader::BlockStatement>(statement->node).statements)) return true;
    } else if (std::get_if<reader::IfStatement>(&statement->node)) {
      auto &ifStmt = std::get<reader::IfStatement>(statement->node);
      if (definitelyReturns(ifStmt.consequent) && definitelyReturns(ifStmt.alternate)) return true;
    } else if (std::get_if<reader::LabeledStatement>(&statement->node)) {
      auto &labeled = std::get<reader::LabeledStatement>(statement->node);
      if (labeled.body != nullptr) {
        std::vector<std::unique_ptr<reader::Statement>> bodyList;
        bodyList.push_back(std::move(const_cast<std::unique_ptr<reader::Statement> &>(labeled.body)));
        if (definitelyReturns(bodyList)) return true;
      }
    } else if (std::get_if<reader::ReturnStatement>(&statement->node)) {
      return true;
    }
  }
  return false;
}

TypePtr Checker::inferCall(const reader::ExprPtr &expr, const reader::CallExpr &call) {
  const TypePtr target = inferExpression(call.callee);
  annotations_.argumentTypes.insert_or_assign(expr.get(), target);

  if (target == nullptr || target->kind != TypeKind::Function)
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::TYPE_CALL_TARGET_EXPECTS_ARGUMENTS, "call", expr->range}};

  return target->result;
}

TypePtr Checker::inferObject(const reader::ExprPtr &, const reader::ObjectExpr &) { return undefinedType(); }

TypePtr Checker::inferArray(const reader::ExprPtr &, const reader::ArrayExpr &) { return undefinedType(); }

} // namespace jsmm::types
