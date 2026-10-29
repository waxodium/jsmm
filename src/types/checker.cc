#include "types/checker.hh"
#include "builtins/builtin.hh"
#include "errors/errors.hh"
#include "types/unifier.hh"
#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace jsmm::types {

TypePtr Checker::unionPayload(TypePtr type) const {
  type = resolve(std::move(type));
  if (type == nullptr || type->kind != TypeKind::Union) return nullptr;
  for (const TypePtr &member : type->members) {
    const TypePtr resolved = resolve(member);
    if (resolved == nullptr) continue;
    if (resolved->kind == TypeKind::Null || resolved->kind == TypeKind::Never) continue;
    return resolved;
  }
  return nullptr;
}

bool Checker::isNumber(TypePtr type) const {
  type = resolve(std::move(type));
  return type != nullptr && (type->kind == TypeKind::Number || type->kind == TypeKind::Byte);
}

Annotations Checker::check(const reader::Program &program) {
  deferredChecks_.clear();
  inferenceVariables_.clear();
  scopes_.push_back(Scope{});
  hoistFunctions(program, functions_);
  hoistVarDeclarations(program.statements, &scopes_.back());
  inferStatements(program.statements);
  recordTopLevelTypes();
  finalize();
  scopes_.pop_back();
  return annotations_;
}

TypePtr Checker::freshVariable() {
  TypePtr variable = typeContext_.variable();
  inferenceVariables_.push_back(variable);
  return variable;
}

TypePtr Checker::BuiltinInference::freshVariable() { return checker_.freshVariable(); }

TypePtr Checker::BuiltinInference::stringType() { return checker_.stringType(); }

TypePtr Checker::BuiltinInference::numberType() { return checker_.numberType(); }

TypePtr Checker::BuiltinInference::booleanType() { return checker_.booleanType(); }

TypePtr Checker::BuiltinInference::undefinedType() { return checker_.nullType(); }

TypePtr Checker::BuiltinInference::resolveType(TypePtr type) {
  return checker_.resolve(std::move(type));
}

std::string Checker::BuiltinInference::showType(TypePtr type) {
  return checker_.typeContext_.show(std::move(type));
}

void Checker::BuiltinInference::report(const reader::SourceRange &range,
                                       const std::string &message) {
  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::TYPE_EXPECTED_KIND, message, range}};
}

void Checker::BuiltinInference::requireUnify(TypePtr left, TypePtr right,
                                             const reader::SourceRange &range,
                                             const std::string &what) {
  if (!checker_.unifies(std::move(left), std::move(right), what))
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, what, range}};
}

void Checker::BuiltinInference::requireConcreteType(TypePtr type,
                                                    const reader::SourceRange &range) {
  checker_.deferredChecks_.push_back([this, type, range]() {
    const TypePtr resolved = checker_.resolve(type);
    if (resolved == nullptr || resolved->kind == TypeKind::Variable)
      report(range, "cannot infer the type of this argument");
  });
}

void Checker::BuiltinInference::deferCheck(std::function<void()> check) {
  checker_.deferredChecks_.push_back(std::move(check));
}

void Checker::hoistFunctions(const reader::Program &program,
                             std::vector<FunctionEntry> &functions) {
  for (const auto &statement : program.statements) {
    if (const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement->node)) {
      std::vector<reader::FunctionParameter> parameters;
      parameters.reserve(declaration->parameters.size());
      for (const auto &p : declaration->parameters) parameters.push_back(p);
      reader::FunctionDeclaration hoisted{declaration->name, std::move(parameters), nullptr,
                                          declaration->returnAnnotation};
      FunctionEntry entry{std::move(hoisted), freshVariable(), {}, &scopes_.back()};
      entry.parameterTypes.reserve(declaration->parameters.size());
      for (std::size_t index = 0; index < declaration->parameters.size(); ++index)
        entry.parameterTypes.push_back(freshVariable());
      functions.push_back(std::move(entry));
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

void Checker::inferFunctionBody(FunctionEntry &entry, reader::BlockStatement *body,
                                Scope *parentScope) {
  (void)parentScope;
  Scope scope;
  for (std::size_t index = 0; index < entry.declaration.parameters.size(); ++index)
    scope.bindings.insert_or_assign(entry.declaration.parameters[index].name,
                                    entry.parameterTypes[index]);
  scopes_.push_back(std::move(scope));
  currentFunctions_.push_back(&entry);
  returnTypeStack_.emplace_back();
  for (const auto &stmt : body->statements) inferStatement(*stmt);
  scopes_.pop_back();
  currentFunctions_.pop_back();

  const TypePtr declared = entry.declaration.returnAnnotation != nullptr
                               ? resolveTypeAnnotation(entry.declaration.returnAnnotation)
                               : nullptr;
  TypePtr computed = nullType();
  for (const auto &rt : returnTypeStack_.back()) {
    if (declared != nullptr) {
      if (!unifies(declared, rt, "return"))
        throw errors::ErrorException{
            errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, "return", {}}};
      continue;
    }
    if (computed == nullptr || computed->kind == TypeKind::Null) {
      computed = rt;
    } else if (!unifies(computed, rt, "return")) {
      computed = typeContext_.unionType({computed, rt});
    }
  }
  if (declared != nullptr) computed = declared;
  returnTypeStack_.pop_back();

  if (entry.returnType != nullptr && entry.returnType->kind == TypeKind::Variable &&
      entry.returnType->link == nullptr) {
    entry.returnType->link = computed;
  } else {
    entry.returnType = computed;
  }
}

void Checker::inferStatements(const reader::StatementList &statements) {
  for (const auto &statement : statements) inferStatement(*statement);
}

void Checker::inferDeclaration(const reader::VariableDeclaration &declaration) {
  if (declaration.kind == "type") {
    for (const auto &declarator : declaration.declarations) {
      TypePtr resolved = resolveTypeAnnotation(declarator.annotation);
      typeAliases_.insert_or_assign(declarator.name, resolved);
    }
    return;
  }

  for (auto &declarator : declaration.declarations) {
    TypePtr initializerType;
    TypePtr declaredType = nullptr;

    // Resolve type annotation if present
    if (declarator.annotation != nullptr) {
      declaredType = resolveTypeAnnotation(declarator.annotation);
      // void cannot be used as a variable type
      if (declaredType == nullptr &&
          !declarator.annotation->names.empty() &&
          declarator.annotation->names[0] == "void")
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_UNKNOWN_TYPE_NAME, "void", declarator.annotation->range}};
    }

    if (declarator.initializer != nullptr) {
      initializerType = inferExpression(declarator.initializer);

      // Check array element type is not void
      if (initializerType != nullptr && initializerType->kind == TypeKind::Array &&
          initializerType->element == nullptr)
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_UNKNOWN_TYPE_NAME, "void", declarator.range}};

      // If a type is declared, check compatibility
      if (declaredType != nullptr && initializerType != nullptr) {
        // If declared type is a primitive but initializer is an array,
        // treat the variable as an array of that primitive type
        TypePtr effectiveDeclared = declaredType;
        if (initializerType->kind == TypeKind::Array &&
            (declaredType->kind == TypeKind::Number || declaredType->kind == TypeKind::String ||
             declaredType->kind == TypeKind::Boolean))
          effectiveDeclared = typeContext_.arrayType(declaredType);

        if (!unifies(effectiveDeclared, initializerType, "declaration"))
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_CANNOT_UNIFY, declarator.name, declarator.range}};
      }
    }

    // Track const variables
    if (declaration.kind == "const") {
      scopes_.back().constants.insert(declarator.name);
    }

    TypePtr bindingType = declaredType != nullptr ? declaredType : initializerType;
    // If initializer is an array and declared type is primitive, binding is array of primitive
    if (initializerType != nullptr && initializerType->kind == TypeKind::Array &&
        declaredType != nullptr &&
        (declaredType->kind == TypeKind::Number || declaredType->kind == TypeKind::String ||
         declaredType->kind == TypeKind::Boolean))
      bindingType = typeContext_.arrayType(declaredType);

    // Constants may hold at most one non-nullable union member
    if (declaration.kind == "const" && bindingType != nullptr &&
        bindingType->kind == TypeKind::Union) {
      int payloadCount = 0;
      for (const auto &member : bindingType->members) {
        const TypePtr resolved = resolve(member);
        if (resolved != nullptr && resolved->kind != TypeKind::Null)
          ++payloadCount;
      }
      if (payloadCount > 1)
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_UNION_REQUIRES_SINGLE_PAYLOAD, declarator.name,
            declarator.range}};
    }

    // Store the resolved binding type for codegen coercion and zero values
    if (bindingType != nullptr) {
      annotations_.declarationTypes.insert_or_assign(&declarator, bindingType);
    }
    const auto existing = scopes_.back().bindings.find(declarator.name);
    if (existing != scopes_.back().bindings.end() && existing->second == nullptr) {
      scopes_.back().bindings[declarator.name] = bindingType;
    } else if (bindingType != nullptr) {
      if (!unifies(existing == scopes_.back().bindings.end() ? nullptr : existing->second,
                   bindingType, "assignment"))
        throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY,
                                                         declarator.name, declarator.range}};
      if (existing == scopes_.back().bindings.end()) {
        scopes_.back().bindings.insert_or_assign(declarator.name, bindingType);
      } else {
        existing->second = bindingType;
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
          bool hoisted = false;
          for (auto &function : functions_)
            if (function.declaration.name == node.name) {
              inferFunctionBody(function, node.body.get(), &scopes_.back());
              hoisted = true;
              break;
            }
          if (!hoisted)
            throw errors::ErrorException{
                errors::ErrorStatus{errors::ErrorCode::TYPE_FUNCTION_DECLARATIONS_MUST_BE_TOP_LEVEL,
                                    node.name, statement.range}};
        } else if constexpr (std::is_same_v<T, reader::ReturnStatement>) {
          if (currentFunctions_.empty()) {
            throw errors::ErrorException{errors::ErrorStatus{
                errors::ErrorCode::TYPE_RETURN_OUTSIDE_FUNCTION, "return", statement.range}};
          }
          if (node.argument != nullptr) {
            const TypePtr argument = inferExpression(node.argument);
            returnTypeStack_.back().push_back(argument);
          } else {
            returnTypeStack_.back().push_back(nullType());
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
          ++loopDepth_;
          inferStatements(node.body);
          --loopDepth_;
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          if (node.init != nullptr) inferDeclaration(*node.init);
          if (node.initExpression != nullptr) (void)inferExpression(node.initExpression);
          if (node.condition != nullptr) (void)inferExpression(node.condition);
          if (node.update != nullptr) (void)inferExpression(node.update);
          ++loopDepth_;
          inferStatements(node.body);
          --loopDepth_;
        } else if constexpr (std::is_same_v<T, reader::BreakStatement>) {
          if (node.label != nullptr) {
            bool found = false;
            for (auto entry = labels_.rbegin(); entry != labels_.rend(); ++entry)
              if (entry->first == node.label->text) {
                found = true;
                break;
              }
            if (!found)
              throw errors::ErrorException{errors::ErrorStatus{
                  errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER, node.label->text,
                  statement.range}};
          } else if (loopDepth_ == 0) {
            throw errors::ErrorException{errors::ErrorStatus{
                errors::ErrorCode::TYPE_BREAK_OUTSIDE_LOOP, "break", statement.range}};
          }
        } else if constexpr (std::is_same_v<T, reader::ContinueStatement>) {
          if (node.label != nullptr) {
            bool found = false;
            bool loopLabel = false;
            for (auto entry = labels_.rbegin(); entry != labels_.rend(); ++entry)
              if (entry->first == node.label->text) {
                found = true;
                loopLabel = entry->second;
                break;
              }
            if (!found)
              throw errors::ErrorException{errors::ErrorStatus{
                  errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER, node.label->text,
                  statement.range}};
            if (!loopLabel)
              throw errors::ErrorException{errors::ErrorStatus{
                  errors::ErrorCode::TYPE_CONTINUE_OUTSIDE_LOOP, node.label->text,
                  statement.range}};
          } else if (loopDepth_ == 0) {
            throw errors::ErrorException{errors::ErrorStatus{
                errors::ErrorCode::TYPE_CONTINUE_OUTSIDE_LOOP, "continue", statement.range}};
          }
        } else if constexpr (std::is_same_v<T, reader::ThrowStatement>) {
          if (node.argument != nullptr) (void)inferExpression(node.argument);
        } else if constexpr (std::is_same_v<T, reader::LabeledStatement>) {
          if (node.body != nullptr) {
            const bool loopBody =
                std::holds_alternative<reader::WhileStatement>(node.body->node) ||
                std::holds_alternative<reader::ForStatement>(node.body->node);
            labels_.emplace_back(node.label, loopBody);
            inferStatement(*node.body);
            labels_.pop_back();
          }
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
        } else if constexpr (std::is_same_v<T, reader::AddressOfExpr>) {
          return inferAddressOf(expr, node);
        } else if constexpr (std::is_same_v<T, reader::DereferenceExpr>) {
          return inferDereference(expr, node);
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
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "undefined", expr->range}};
        }
        return neverType();
      },
      expr->node);
  annotations_.expressionTypes.insert_or_assign(expr.get(), type);
  return type;
}

TypePtr Checker::inferIdentifier(const reader::ExprPtr &expr,
                                 const reader::IdentifierExpr &identifier) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->bindings.find(identifier.name);
    if (found != scope->bindings.end()) {
      if (found->second != nullptr) return found->second;
      return nullType();
    }
  }
  for (const auto &function : functions_) {
    if (function.declaration.name == identifier.name) {
      TypePtr resultType = function.returnType ? function.returnType : nullType();
      TypePtr calleeType = this->makeFunctionType(resultType, function.parameterTypes);
      annotations_.argumentTypes.insert_or_assign(expr.get(), calleeType);
      annotations_.expressionTypes.insert_or_assign(expr.get(), calleeType);
      return calleeType;
    }
  }
  throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER,
                                                   identifier.name, expr->range}};
}

TypePtr Checker::inferUnary(const reader::ExprPtr &expr, const reader::UnaryExpr &unary) {
  if (unary.op == "!") {
    const TypePtr operand = inferExpression(unary.operand);
    annotations_.argumentTypes.insert_or_assign(expr.get(), operand);
    return booleanType();
  }
  if (unary.op == "~" || unary.op == "-") {
    const TypePtr operand = inferExpression(unary.operand);
    if (!requireNumeric(operand))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, unary.op, expr->range}};
    annotations_.argumentTypes.insert_or_assign(expr.get(), operand);
    return numberType();
  }
  if (unary.op == "+") {
    const TypePtr operand = inferExpression(unary.operand);
    if (!requireNumeric(operand))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, unary.op, expr->range}};
    return numberType();
  }
  if (unary.op == "typeof") {
    (void)inferExpression(unary.operand);
    return stringType();
  }
  if (unary.op == "void") {
    (void)inferExpression(unary.operand);
    return nullType();
  }
  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, unary.op, expr->range}};
}

TypePtr Checker::inferAddressOf(const reader::ExprPtr &expr, const reader::AddressOfExpr &addrOf) {
  if (!isAddressable(addrOf.operand)) {
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, "address-of", expr->range}};
  }
  const TypePtr operandType = inferExpression(addrOf.operand);
  return pointerType(operandType);
}

TypePtr Checker::inferDereference(const reader::ExprPtr &expr, const reader::DereferenceExpr &deref) {
  const TypePtr pointerType = resolve(inferExpression(deref.operand));
  if (pointerType == nullptr || pointerType->kind != TypeKind::Pointer) {
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "dereference", expr->range}};
  }
  return pointerType->pointed;
}

TypePtr Checker::inferBinary(const reader::ExprPtr &expr, const reader::BinaryExpr &binary) {
  const TypePtr left = inferExpression(binary.left);
  const TypePtr right = inferExpression(binary.right);
  annotations_.argumentTypes.insert_or_assign(expr.get(), left);

  if (binary.op == "||" || binary.op == "&&" || binary.op == "??") {
    if (!unifies(left, right, binary.op))
      throw errors::ErrorException{errors::ErrorStatus{
          errors::ErrorCode::TYPE_UNION_REQUIRES_SINGLE_PAYLOAD, binary.op, expr->range}};
    if (binary.op == "??") { return left; }
    return left;
  }

  if (binary.op == "==" || binary.op == "!=" || binary.op == "===" || binary.op == "!==") {
    if (!unifies(left, right, binary.op))
      throw errors::ErrorException{errors::ErrorStatus{
          errors::ErrorCode::TYPE_UNION_REQUIRES_SINGLE_PAYLOAD, binary.op, expr->range}};
    return booleanType();
  }

  if (binary.op == "+" || binary.op == "-" || binary.op == "*" || binary.op == "/" ||
      binary.op == "%" || binary.op == "**") {
    const TypePtr resolvedLeft = resolve(left);
    const TypePtr resolvedRight = resolve(right);
    if (resolvedLeft != nullptr && resolvedLeft->kind == TypeKind::Pointer &&
        resolvedRight != nullptr && resolvedRight->kind == TypeKind::Pointer)
      return numberType();
    if (resolvedLeft != nullptr && resolvedLeft->kind == TypeKind::Pointer && requireNumeric(right))
      return pointerType(resolvedLeft->pointed);
    if (!requireNumeric(left) || !requireNumeric(right))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       binary.op, expr->range}};
    return numberType();
  }

  if (binary.op == "<" || binary.op == ">" || binary.op == "<=" || binary.op == ">=") {
    if (!requireNumeric(left) || !requireNumeric(right))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       binary.op, expr->range}};
    return booleanType();
  }

  if (binary.op == "&" || binary.op == "|" || binary.op == "^" || binary.op == "<<" ||
      binary.op == ">>" || binary.op == ">>>") {
    if (!requireNumeric(left) || !requireNumeric(right))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       binary.op, expr->range}};
    return numberType();
  }

  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, binary.op, expr->range}};
}

TypePtr Checker::inferUpdate(const reader::ExprPtr &expr, const reader::UpdateExpr &update) {
  if (!isAddressable(update.operand))
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, update.op, expr->range}};
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&update.operand->node)) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
      if (scope->constants.contains(identifier->name))
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_CANNOT_ASSIGN_TO_CONST, identifier->name, expr->range}};
  }
  TypePtr target = targetType(update.operand);
  if (!requireNumeric(target))
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, update.op, expr->range}};
  annotations_.argumentTypes.insert_or_assign(expr.get(), target);
  return numberType();
}

TypePtr Checker::inferAssignment(const reader::ExprPtr &expr,
                                 const reader::AssignmentExpr &assignment) {
  TypePtr right = inferExpression(assignment.right);
  TypePtr target = targetType(assignment.left);

  // Check if assigning to a const variable
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&assignment.left->node)) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
      const auto found = scope->constants.find(identifier->name);
      if (found != scope->constants.end()) {
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_CANNOT_ASSIGN_TO_CONST, identifier->name, expr->range}};
      }
    }
  }

  if (assignment.op != "=") {
    if (!requireNumeric(target) || !requireNumeric(right))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       assignment.op, expr->range}};
    annotations_.argumentTypes.insert_or_assign(expr.get(), right);
    return numberType();
  }

  if (!unifies(target, right, "assignment"))
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, "assignment", expr->range}};
  annotations_.argumentTypes.insert_or_assign(expr.get(), right);
  return right;
}

TypePtr Checker::targetType(const reader::ExprPtr &target) {
  const TypePtr type = inferExpression(target);
  if (type == nullptr || type->kind == TypeKind::Variable)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, "assignment", target->range}};
  return type;
}

bool Checker::isAddressable(const reader::ExprPtr &expr) const {
  if (std::get_if<reader::IdentifierExpr>(&expr->node)) return true;
  if (std::get_if<reader::DereferenceExpr>(&expr->node)) return true;
  if (std::get_if<reader::MemberExpr>(&expr->node)) return true;
  return false;
}

TypePtr Checker::memberResultType(const reader::MemberExpr &member,
                                  const reader::Expression &expression, TypePtr objectType) {
  if (objectType != nullptr && objectType->kind == TypeKind::Pointer)
    objectType = resolve(objectType->pointed);
  if (member.computedFlag) {
    if (objectType != nullptr && objectType->kind == TypeKind::Array) return objectType->element;
    if (objectType != nullptr && objectType->kind == TypeKind::Bytes) return byteType();
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "[]", expression.range}};
  }
  if (objectType != nullptr && objectType->kind == TypeKind::Struct) {
    for (const auto &[fieldName, fieldType] : objectType->fields) {
      if (fieldName == member.property) return fieldType;
    }
  }
  throw errors::ErrorException{errors::ErrorStatus{
      errors::ErrorCode::TYPE_MEMBER_ACCESS_NOT_SUPPORTED, member.property, expression.range}};
}

TypePtr Checker::inferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member) {
  TypePtr objectType = resolve(inferExpression(member.object));
  if (member.computedFlag) {
    TypePtr indexType = inferExpression(member.computed);
    if (!requireNumeric(indexType))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "[]", expr->range}};
  }
  if (objectType == nullptr || objectType->kind == TypeKind::Variable) {
    TypePtr result = freshVariable();
    annotations_.argumentTypes.insert_or_assign(expr.get(), objectType);
    deferMember(expr, member, objectType, result);
    return result;
  }
  annotations_.argumentTypes.insert_or_assign(expr.get(), objectType);
  return memberResultType(member, *expr, objectType);
}

void Checker::deferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member,
                          TypePtr objectType, TypePtr result) {
  (void)objectType;
  pendingMembers_.push_back(std::make_pair(expr.get(), member));
  pendingTypeResolutions_.insert_or_assign(expr.get(), result);
}

void Checker::resolvePendingMembers() {
  for (const auto &entry : pendingMembers_) {
    const reader::Expression *expression = entry.first;
    const reader::MemberExpr &member = entry.second;
    const auto pendingResult = pendingTypeResolutions_.find(expression);
    if (pendingResult == pendingTypeResolutions_.end()) continue;
    const TypePtr result = pendingResult->second;
    TypePtr objectType = nullptr;
    const auto object = annotations_.expressionTypes.find(member.object.get());
    if (object != annotations_.expressionTypes.end()) objectType = resolve(object->second);
    if (objectType != nullptr && objectType->kind == TypeKind::Variable) {
      deferredChecks_.push_back([this, expression, member, result]() {
        const auto object = annotations_.expressionTypes.find(member.object.get());
        const TypePtr resolvedObject =
            object == annotations_.expressionTypes.end() ? nullptr : resolve(object->second);
        const TypePtr memberType = memberResultType(member, *expression, resolvedObject);
        if (result->kind == TypeKind::Variable && result->link == nullptr)
          result->link = memberType;
      });
      continue;
    }
    const TypePtr memberType = memberResultType(member, *expression, objectType);
    if (result->kind == TypeKind::Variable && result->link == nullptr)
      result->link = memberType;
    else if (!unifies(result, memberType, "member"))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, "member", expression->range}};
  }
  pendingMembers_.clear();
  pendingTypeResolutions_.clear();
}

void Checker::bindUnresolvedTypes(bool excludeMemberResults) { (void)excludeMemberResults; }

void Checker::recordTopLevelTypes() {
  for (auto &function : functions_) {
    if (function.returnType == nullptr) function.returnType = nullType();
    annotations_.functionTypes.insert_or_assign(
        function.declaration.name, makeFunctionType(function.returnType, function.parameterTypes));
  }
}

void Checker::finalize() {
  resolvePendingMembers();
  bindUnresolvedTypes(false);
  for (const auto &variable : inferenceVariables_) {
    if (variable != nullptr && variable->kind == TypeKind::Variable && variable->link == nullptr)
      variable->link = numberType();
  }
  for (auto &check : deferredChecks_) check();
  deferredChecks_.clear();
  for (auto it = annotations_.expressionTypes.begin(); it != annotations_.expressionTypes.end();) {
    if (it->second == nullptr) {
      it = annotations_.expressionTypes.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = annotations_.declarationTypes.begin();
       it != annotations_.declarationTypes.end();) {
    if (it->second == nullptr) {
      it = annotations_.declarationTypes.erase(it);
    } else {
      ++it;
    }
  }
}

bool Checker::requireNumeric(TypePtr type) {
  type = resolve(std::move(type));
  if (type == nullptr) return true;
  if (type->kind == TypeKind::Variable) {
    Unifier unifier(typeContext_);
    return unifier.unify(type, numberType());
  }
  return isNumber(type);
}

bool Checker::unifies(TypePtr left, TypePtr right, std::string_view context) {
  if (left == nullptr || right == nullptr) return true;
  left = resolve(left);
  right = resolve(right);
  if (left == right) return true;
  if (left == nullptr || right == nullptr) return true;
  // Undefined is compatible with anything (uninitialized)
  if (right->kind == TypeKind::Null) return true;
  if (left->kind == TypeKind::Variable || right->kind == TypeKind::Variable) {
    Unifier unifier(typeContext_);
    return unifier.unify(left, right);
  }
  if (left->kind == TypeKind::Union && right->kind == TypeKind::Union) {
    for (const auto &member : right->members)
      if (!unifies(left, member, context)) return false;
    return true;
  }
  if (left->kind == TypeKind::Union) {
    // A value can be assigned to a union if it matches at least one member
    for (const auto &member : left->members)
      if (unifies(member, right, context)) return true;
    return false;
  }
  if (right->kind == TypeKind::Union) {
    // A union value can be assigned to a type only if all members match
    for (const auto &member : right->members)
      if (!unifies(left, member, context)) return false;
    return true;
  }
  // byte is compatible with number (byte literals are numbers)
  if ((left->kind == TypeKind::Byte && right->kind == TypeKind::Number) ||
      (left->kind == TypeKind::Number && right->kind == TypeKind::Byte))
    return true;
  // bytes is compatible with arrays of number/byte, and bytes elements with number
  if ((left->kind == TypeKind::Bytes && right->kind == TypeKind::Array) ||
      (left->kind == TypeKind::Array && right->kind == TypeKind::Bytes))
    return true;
  if ((left->kind == TypeKind::Bytes && right->kind == TypeKind::Number) ||
      (left->kind == TypeKind::Number && right->kind == TypeKind::Bytes))
    return true;
  if (left->kind != right->kind) return false;
  if (left->kind == TypeKind::Pointer) {
    return unifies(left->pointed, right->pointed, context);
  }
  if (left->kind == TypeKind::Array) {
    return unifies(left->element, right->element, context);
  }
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
      if (definitelyReturns(std::get<reader::BlockStatement>(statement->node).statements))
        return true;
    } else if (std::get_if<reader::IfStatement>(&statement->node)) {
      auto &ifStmt = std::get<reader::IfStatement>(statement->node);
      if (definitelyReturns(ifStmt.consequent) && definitelyReturns(ifStmt.alternate)) return true;
    } else if (std::get_if<reader::LabeledStatement>(&statement->node)) {
      auto &labeled = std::get<reader::LabeledStatement>(statement->node);
      if (labeled.body != nullptr) {
        std::vector<std::unique_ptr<reader::Statement>> bodyList;
        bodyList.push_back(
            std::move(const_cast<std::unique_ptr<reader::Statement> &>(labeled.body)));
        if (definitelyReturns(bodyList)) return true;
      }
    } else if (std::get_if<reader::ReturnStatement>(&statement->node)) {
      return true;
    }
  }
  return false;
}

TypePtr Checker::inferCall(const reader::ExprPtr &expr, const reader::CallExpr &call) {
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&call.callee->node)) {
    if (const builtins::BuiltinSpec *builtin =
            builtins::BuiltinRegistry::instance().find(identifier->name)) {
      if (!builtin->variadic && call.arguments.size() != builtin->minimumArguments) {
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_BUILTIN_EXPECTS_ARGUMENTS, identifier->name, expr->range}};
      }
      if (call.arguments.size() < builtin->minimumArguments) {
        throw errors::ErrorException{
            errors::ErrorStatus{errors::ErrorCode::TYPE_BUILTIN_EXPECTS_AT_LEAST_ARGUMENTS,
                                identifier->name, expr->range}};
      }
      std::vector<TypePtr> argumentTypes;
      argumentTypes.reserve(call.arguments.size());
      for (const auto &arg : call.arguments) argumentTypes.push_back(inferExpression(arg));
      const TypePtr result = builtin->infer(builtinInference_, argumentTypes, expr->range);
      TypePtr calleeType = this->makeFunctionType(result, argumentTypes);
      annotations_.argumentTypes.insert_or_assign(expr.get(), calleeType);
      return result;
    }
  }
  const TypePtr target = inferExpression(call.callee);
  annotations_.argumentTypes.insert_or_assign(expr.get(), target);

  if (target == nullptr || target->kind != TypeKind::Function)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_CALL_TARGET_EXPECTS_ARGUMENTS, "call", expr->range}};

  if (target->parameters.size() != call.arguments.size()) {
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_BUILTIN_EXPECTS_ARGUMENTS, "call", expr->range}};
  }

  for (std::size_t i = 0; i < call.arguments.size(); ++i) {
    const TypePtr argType = inferExpression(call.arguments[i]);
    if (!unifies(target->parameters[i], argType, "argument"))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY,
                                                       "argument", call.arguments[i]->range}};
  }

  return target->result;
}

TypePtr Checker::inferObject(const reader::ExprPtr &expr, const reader::ObjectExpr &object) {
  (void)expr;
  std::vector<std::pair<std::string, TypePtr>> fields;
  fields.reserve(object.properties.size());
  for (const auto &[key, value] : object.properties) {
    TypePtr type = inferExpression(value);
    fields.emplace_back(key, type);
  }
  return typeContext_.structType(std::move(fields));
}

TypePtr Checker::inferArray(const reader::ExprPtr &expr, const reader::ArrayExpr &array) {
  (void)expr;
  TypePtr element = nullType();
  for (const auto &elem : array.elements) {
    TypePtr elemType = inferExpression(elem);
    if (element == nullptr || element->kind == TypeKind::Null) {
      element = elemType;
    } else if (!unifies(element, elemType, "array")) {
      element = typeContext_.unionType({element, elemType});
    }
  }
  for (const auto &desig : array.designated) {
    TypePtr elemType = inferExpression(desig.value);
    if (element == nullptr || element->kind == TypeKind::Null) {
      element = elemType;
    } else if (!unifies(element, elemType, "array")) {
      element = typeContext_.unionType({element, elemType});
    }
  }
  return typeContext_.arrayType(element);
}

TypePtr Checker::resolveTypeAnnotation(const std::shared_ptr<reader::TypeExpr> &annotation) {
  if (annotation == nullptr || annotation->names.empty()) return nullptr;

  // Handle int(size, signed/unsigned) type expressions
  if (annotation->isIntType) {
    // For now, int types map to number (the unified numeric type)
    return numberType();
  }

  // Resolve each name in the annotation
  std::vector<TypePtr> resolved;
  resolved.reserve(annotation->names.size());
  for (std::size_t index = 0; index < annotation->names.size(); ++index) {
    const std::string &name = annotation->names[index];
    TypePtr type = nullptr;

    // Check built-in types
    if (name == "number") type = numberType();
    else if (name == "string") type = stringType();
    else if (name == "bool") type = booleanType();
    else if (name == "boolean") type = booleanType();
    else if (name == "byte") type = byteType();
    else if (name == "bytes") type = bytesType();
    else if (name == "void") type = nullptr; // void is not a valid variable type
    else if (name == "auto") type = nullptr; // auto is handled by inference
    else if (name == "null") type = nullType();

    // Check type aliases (skip for auto, which is intentionally null)
    if (type == nullptr && name != "auto") {
      const auto found = typeAliases_.find(name);
      if (found != typeAliases_.end()) {
        type = found->second;
      } else {
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_UNKNOWN_TYPE_NAME, name, annotation->range}};
      }
    }

    // Apply pointer modifier
    if (annotation->pointer) {
      type = pointerType(type);
    }

    // Apply array size modifier to the final member of a union annotation
    if (annotation->isArray && type != nullptr && index + 1 == annotation->names.size()) {
      type = typeContext_.arrayType(type);
    }

    resolved.push_back(type);
  }

  if (resolved.size() == 1) return resolved[0];
  return typeContext_.unionType(std::move(resolved));
}

} // namespace jsmm::types
