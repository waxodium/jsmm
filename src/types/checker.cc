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

bool Checker::isNumber(TypePtr type) const {
  type = resolve(std::move(type));
  return type != nullptr &&
         (type->kind == TypeKind::Number || type->kind == TypeKind::Byte ||
          type->kind == TypeKind::Integer);
}

bool Checker::isString(TypePtr type) const {
  type = resolve(std::move(type));
  return type != nullptr && type->kind == TypeKind::String;
}

bool Checker::isBytes(TypePtr type) const {
  type = resolve(std::move(type));
  return type != nullptr && type->kind == TypeKind::Bytes;
}

bool Checker::isText(TypePtr type) const { return isString(type) || isBytes(type); }

Annotations Checker::check(const reader::Program &program) {
  deferredChecks_.clear();
  inferenceVariables_.clear();
  scopes_.push_back(Scope{});
  hoistFunctions(program, functions_);
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
      reader::FunctionDeclaration hoisted{declaration->name, declaration->parameters, nullptr,
                                          declaration->returnAnnotation};
      FunctionEntry entry{std::move(hoisted), freshVariable(), {}, &scopes_.back()};
      entry.parameterTypes.reserve(declaration->parameters.size());
      for (std::size_t index = 0; index < declaration->parameters.size(); ++index) {
        entry.parameterTypes.push_back(freshVariable());
      }
      functions.push_back(std::move(entry));
    }
  }
}

void Checker::inferFunctionBody(FunctionEntry &entry, reader::BlockStatement *body) {
  Scope scope;
  std::size_t fixedIndex = 0;
  for (std::size_t index = 0; index < entry.declaration.parameters.size(); ++index) {
    const reader::FunctionParameter &parameter = entry.declaration.parameters[index];
    if (fixedIndex >= entry.parameterTypes.size()) break;
    if (parameter.annotation != nullptr) {
      const TypePtr annotated = resolveTypeAnnotation(parameter.annotation);
      if (annotated != nullptr)
        (void)unifies(annotated, entry.parameterTypes[fixedIndex], "parameter");
    }
    scope.bindings.insert_or_assign(parameter.name, entry.parameterTypes[fixedIndex]);
    ++fixedIndex;
  }
  scopes_.push_back(std::move(scope));
  currentFunctions_.push_back(&entry);
  returnTypeStack_.emplace_back();
  for (const auto &stmt : body->statements) inferStatement(*stmt);
  scopes_.pop_back();
  currentFunctions_.pop_back();

  const TypePtr declared = entry.declaration.returnAnnotation != nullptr
                               ? resolveTypeAnnotation(entry.declaration.returnAnnotation)
                               : nullptr;
  const bool returnsVoid = entry.declaration.returnAnnotation != nullptr &&
                           !entry.declaration.returnAnnotation->names.empty() &&
                           entry.declaration.returnAnnotation->names[0] == "void";
  TypePtr computed = nullType();
  for (const auto &rt : returnTypeStack_.back()) {
    if (returnsVoid) {
      const TypePtr resolved = resolve(rt);
      if (resolved != nullptr && resolved->kind != TypeKind::Null &&
          resolved->kind != TypeKind::Never)
        throw errors::ErrorException{
            errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, "return", {}}};
      continue;
    }
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

      if (declaredType != nullptr && declaredType->kind == TypeKind::Array &&
          std::holds_alternative<reader::ArrayExpr>(declarator.initializer->node)) {
        const auto &literal = std::get<reader::ArrayExpr>(declarator.initializer->node);
        std::size_t literalSize = literal.elements.size();
        for (const auto &designated : literal.designated)
          if (designated.index + 1 > literalSize) literalSize = designated.index + 1;
        if (declaredType->length > 0 && literalSize > declaredType->length)
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_CANNOT_UNIFY, declarator.name, declarator.range}};
        annotations_.expressionTypes.insert_or_assign(declarator.initializer.get(),
                                                     declaredType);
      }

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
              inferFunctionBody(function, node.body.get());
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
          scopes_.push_back(Scope{});
          inferStatements(node.consequent);
          scopes_.pop_back();
          scopes_.push_back(Scope{});
          inferStatements(node.alternate);
          scopes_.pop_back();
        } else if constexpr (std::is_same_v<T, reader::WhileStatement>) {
          (void)inferExpression(node.condition);
          scopes_.push_back(Scope{});
          ++loopDepth_;
          ++breakableDepth_;
          inferStatements(node.body);
          --breakableDepth_;
          --loopDepth_;
          scopes_.pop_back();
        } else if constexpr (std::is_same_v<T, reader::SwitchStatement>) {
          (void)inferExpression(node.discriminant);
          scopes_.push_back(Scope{});
          ++breakableDepth_;
          for (const auto &clause : node.cases) {
            if (clause.value != nullptr) (void)inferExpression(clause.value);
            inferStatements(clause.statements);
          }
          --breakableDepth_;
          scopes_.pop_back();
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          scopes_.push_back(Scope{});
          if (node.init != nullptr) inferDeclaration(*node.init);
          if (node.initExpression != nullptr) (void)inferExpression(node.initExpression);
          if (node.condition != nullptr) (void)inferExpression(node.condition);
          if (node.update != nullptr) (void)inferExpression(node.update);
          ++breakableDepth_;
          ++loopDepth_;
          scopes_.push_back(Scope{});
          inferStatements(node.body);
          scopes_.pop_back();
          --loopDepth_;
          --breakableDepth_;
          scopes_.pop_back();
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
          } else if (breakableDepth_ == 0) {
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
        } else if constexpr (std::is_same_v<T, reader::BytesLiteralExpr>) {
          return bytesType();
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
        } else if constexpr (std::is_same_v<T, reader::TypeOperandExpr>) {
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_TYPE_USED_AS_VALUE, {}, expr->range}};
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

  if (binary.op == "||" || binary.op == "&&") {
    if (!unifies(left, right, binary.op))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, binary.op, expr->range}};
    return left;
  }

  if (binary.op == "==" || binary.op == "!=") {
    if (!unifies(left, right, binary.op))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, binary.op, expr->range}};
    return booleanType();
  }

  if (binary.op == "+" && (isText(left) || isText(right))) {
    if (!isText(left) || !isText(right))
      throw errors::ErrorException{errors::ErrorStatus{
          errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, binary.op, expr->range}};
    if (isString(left) && isString(right)) return stringType();
    return bytesType();
  }

  if (binary.op == "+" || binary.op == "-" || binary.op == "*" || binary.op == "/" ||
      binary.op == "%" || binary.op == "**") {
    const TypePtr resolvedLeft = resolve(left);
    const TypePtr resolvedRight = resolve(right);
    const bool pointerOperands =
        (resolvedLeft != nullptr && resolvedLeft->kind == TypeKind::Pointer) ||
        (resolvedRight != nullptr && resolvedRight->kind == TypeKind::Pointer);
    if (pointerOperands && binary.op != "+" && binary.op != "-")
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       binary.op, expr->range}};
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
    const TypePtr orderLeft = resolve(left);
    const TypePtr orderRight = resolve(right);
    if (orderLeft != nullptr && orderLeft->kind == TypeKind::Pointer && orderRight != nullptr &&
        orderRight->kind == TypeKind::Pointer)
      return booleanType();
    if (isText(left) && isText(right)) return booleanType();
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
    const TypePtr resolvedLeft = resolve(left);
    const TypePtr resolvedRight = resolve(right);
    if (resolvedLeft == nullptr || resolvedRight == nullptr ||
        resolvedLeft->kind != resolvedRight->kind)
      throw errors::ErrorException{errors::ErrorStatus{
          errors::ErrorCode::TYPE_CANNOT_UNIFY, binary.op, expr->range}};
    if (resolvedLeft->kind == TypeKind::Integer &&
        (resolvedLeft->length != resolvedRight->length ||
         resolvedLeft->isSigned != resolvedRight->isSigned))
      throw errors::ErrorException{errors::ErrorStatus{
          errors::ErrorCode::TYPE_CANNOT_UNIFY, binary.op, expr->range}};
    if (!requireNumeric(left) || !requireNumeric(right))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                        binary.op, expr->range}};
    return numberType();
  }

  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, binary.op, expr->range}};
}

TypePtr Checker::inferAssignment(const reader::ExprPtr &expr,
                                 const reader::AssignmentExpr &assignment) {
  TypePtr right = inferExpression(assignment.right);
  TypePtr target = resolve(inferExpression(assignment.left));
  const bool deferredTarget =
      target != nullptr && target->kind == TypeKind::Variable &&
      std::get_if<reader::DereferenceExpr>(&assignment.left->node) != nullptr;
  if (target == nullptr || (target->kind == TypeKind::Variable && !deferredTarget))
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, "assignment", assignment.left->range}};
  if (isReadOnlyLength(assignment.left))
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, "length", expr->range}};

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
    const TypePtr resolvedTarget = resolve(target);
    if ((assignment.op == "+=" || assignment.op == "-=") && resolvedTarget != nullptr &&
        resolvedTarget->kind == TypeKind::Pointer) {
      if (!requireNumeric(right))
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, assignment.op, expr->range}};
      annotations_.argumentTypes.insert_or_assign(expr.get(), right);
      return target;
    }
    if (assignment.op == "+=" && isText(target) && isText(right)) {
      annotations_.argumentTypes.insert_or_assign(expr.get(), right);
      return target;
    }
    if (isText(target) || isText(right))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       assignment.op, expr->range}};
    if (!requireNumeric(target) || !requireNumeric(right))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       assignment.op, expr->range}};
    annotations_.argumentTypes.insert_or_assign(expr.get(), right);
    return numberType();
  }

  if (deferredTarget) {
    const TypePtr expectedTarget = target;
    const TypePtr assignedType = right;
    const reader::SourceRange range = expr->range;
    deferredChecks_.push_back([this, expectedTarget, assignedType, range]() {
      if (!unifies(expectedTarget, assignedType, "assignment"))
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_CANNOT_UNIFY, "assignment", range}};
    });
    annotations_.argumentTypes.insert_or_assign(expr.get(), right);
    return right;
  }
  if (!unifies(target, right, "assignment"))
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY, "assignment", expr->range}};
  annotations_.argumentTypes.insert_or_assign(expr.get(), right);
  return right;
}

TypePtr Checker::targetType(const reader::ExprPtr &target) {
  const TypePtr type = resolve(inferExpression(target));
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

bool Checker::isReadOnlyLength(const reader::ExprPtr &target) const {
  const auto *member = std::get_if<reader::MemberExpr>(&target->node);
  if (member == nullptr || member->computedFlag || member->property != "length") return false;
  const auto object = annotations_.expressionTypes.find(member->object.get());
  if (object == annotations_.expressionTypes.end()) return false;
  TypePtr objectType = resolve(object->second);
  if (objectType != nullptr && objectType->kind == TypeKind::Pointer)
    objectType = resolve(objectType->pointed);
  if (objectType == nullptr) return false;
  return objectType->kind == TypeKind::String || objectType->kind == TypeKind::Bytes ||
         objectType->kind == TypeKind::Array;
}

TypePtr Checker::memberResultType(const reader::MemberExpr &member,
                                  const reader::Expression &expression, TypePtr objectType) {
  const bool pointerObject = objectType != nullptr && objectType->kind == TypeKind::Pointer;
  const TypePtr pointed = pointerObject ? resolve(objectType->pointed) : nullptr;
  if (pointerObject) objectType = pointed;
  if (member.computedFlag) {
    if (pointerObject && pointed != nullptr && pointed->kind != TypeKind::Array &&
        pointed->kind != TypeKind::Bytes)
      return pointed;
    if (objectType != nullptr && objectType->kind == TypeKind::Array) return objectType->element;
    if (objectType != nullptr && objectType->kind == TypeKind::Bytes) return byteType();
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "[]", expression.range}};
  }
  if (objectType != nullptr &&
      (objectType->kind == TypeKind::String || objectType->kind == TypeKind::Bytes ||
       objectType->kind == TypeKind::Array)) {
    if (member.property == "length") return numberType();
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

void Checker::recordTopLevelTypes() {
  for (auto &function : functions_) {
    if (function.returnType == nullptr) function.returnType = nullType();
    annotations_.functionTypes.insert_or_assign(
        function.declaration.name,
        makeFunctionType(function.returnType, function.parameterTypes));
  }
}

void Checker::finalize() {
  resolvePendingMembers();
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
  const auto numericKind = [](TypeKind kind) {
    return kind == TypeKind::Number || kind == TypeKind::Byte || kind == TypeKind::Integer;
  };
  if (numericKind(left->kind) && numericKind(right->kind)) return true;
  if (right->kind == TypeKind::Boolean && numericKind(left->kind)) return true;
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
  if ((left->kind == TypeKind::String && right->kind == TypeKind::Bytes) ||
      (left->kind == TypeKind::Bytes && right->kind == TypeKind::String))
    return true;
  // bytes is compatible with arrays of number/byte, and bytes elements with number
  if ((left->kind == TypeKind::Bytes && right->kind == TypeKind::Array) ||
      (left->kind == TypeKind::Array && right->kind == TypeKind::Bytes)) {
    const TypePtr element = left->kind == TypeKind::Array ? left->element : right->element;
    return isNumber(element);
  }
  if (left->kind != right->kind) return false;
  if (left->kind == TypeKind::Pointer) {
    return unifies(left->pointed, right->pointed, context);
  }
  if (left->kind == TypeKind::Array) {
    return unifies(left->element, right->element, context);
  }
  if (left->kind == TypeKind::Struct) {
    if (left->fields.size() != right->fields.size()) return false;
    for (const auto &field : left->fields) {
      const auto match = std::find_if(right->fields.begin(), right->fields.end(),
                                      [&](const auto &candidate) { return candidate.first == field.first; });
      if (match == right->fields.end()) return false;
      if (!unifies(field.second, match->second, context)) return false;
    }
    return true;
  }
  return true;
}

void Checker::checkCallArguments(
    const TypePtr &function,
    const std::vector<std::pair<TypePtr, reader::SourceRange>> &arguments,
    const reader::SourceRange &range) {
  const std::size_t fixedParameters = function->parameters.size();
  if (arguments.size() != fixedParameters) {
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_BUILTIN_EXPECTS_ARGUMENTS, "call", range}};
  }

  for (std::size_t i = 0; i < arguments.size(); ++i) {
    TypePtr expected = function->parameters[i];
    if (expected != nullptr && !unifies(expected, arguments[i].first, "argument"))
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_CANNOT_UNIFY,
                                                       "argument", arguments[i].second}};
  }
}

TypePtr Checker::inferCall(const reader::ExprPtr &expr, const reader::CallExpr &call) {
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&call.callee->node)) {
    if (const builtins::BuiltinSpec *builtin =
            builtins::BuiltinRegistry::instance().find(identifier->name)) {
      if (builtin->variadic) {
        if (call.arguments.size() < builtin->minimumArguments) {
          throw errors::ErrorException{
              errors::ErrorStatus{errors::ErrorCode::TYPE_BUILTIN_EXPECTS_AT_LEAST_ARGUMENTS,
                                  identifier->name, expr->range}};
        }
      } else if (call.arguments.size() != builtin->minimumArguments) {
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_BUILTIN_EXPECTS_ARGUMENTS, identifier->name, expr->range}};
      }
      std::vector<TypePtr> argumentTypes;
      argumentTypes.reserve(call.arguments.size());
      for (std::size_t index = 0; index < call.arguments.size(); ++index) {
        const bool expectsType =
            index < builtin->operands.size() &&
            builtin->operands[index] == builtins::OperandKind::Type;
        if (!expectsType) {
          argumentTypes.push_back(inferExpression(call.arguments[index]));
          continue;
        }
        const auto *operand = std::get_if<reader::TypeOperandExpr>(&call.arguments[index]->node);
        if (operand == nullptr)
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_TYPE_OPERAND_EXPECTED, identifier->name, expr->range}};
        const TypePtr resolved = resolveTypeAnnotation(operand->annotation);
        if (resolved == nullptr)
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_TYPE_OPERAND_HAS_NO_LAYOUT, identifier->name,
              call.arguments[index]->range}};
        annotations_.expressionTypes.insert_or_assign(call.arguments[index].get(), resolved);
        argumentTypes.push_back(resolved);
      }
      const TypePtr result = builtin->infer(builtinInference_, argumentTypes, expr->range);
      TypePtr calleeType = this->makeFunctionType(result, argumentTypes);
      annotations_.argumentTypes.insert_or_assign(expr.get(), calleeType);
      return result;
    }
  }
  const TypePtr target = resolve(inferExpression(call.callee));
  annotations_.argumentTypes.insert_or_assign(expr.get(), target);

  std::vector<std::pair<TypePtr, reader::SourceRange>> argumentTypes;
  argumentTypes.reserve(call.arguments.size());
  for (const auto &argument : call.arguments)
    argumentTypes.emplace_back(inferExpression(argument), argument->range);

  if (target != nullptr && target->kind == TypeKind::Function) {
    checkCallArguments(target, argumentTypes, expr->range);
    return resolve(target->result);
  }

  if (target == nullptr || target->kind != TypeKind::Variable)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_CALL_TARGET_EXPECTS_ARGUMENTS, "call", expr->range}};

  const TypePtr result = freshVariable();
  const reader::SourceRange range = expr->range;
  deferredChecks_.push_back([this, target, argumentTypes, result, range]() {
    const TypePtr function = resolve(target);
    if (function == nullptr || function->kind != TypeKind::Function)
      throw errors::ErrorException{errors::ErrorStatus{
          errors::ErrorCode::TYPE_CALL_TARGET_EXPECTS_ARGUMENTS, "call", range}};
    checkCallArguments(function, argumentTypes, range);
    if (result->kind == TypeKind::Variable && result->link == nullptr)
      result->link = function->result;
  });
  return result;
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

bool Checker::sameFunctionSignature(TypePtr left, TypePtr right) {
  left = resolve(std::move(left));
  right = resolve(std::move(right));
  if (left == nullptr || right == nullptr) return left == right;
  if (left->kind == TypeKind::Array && right->kind == TypeKind::Array)
    return sameFunctionSignature(left->element, right->element);
  if (left->kind != TypeKind::Function || right->kind != TypeKind::Function) return false;
  if (left->parameters.size() != right->parameters.size()) return false;
  for (std::size_t i = 0; i < left->parameters.size(); ++i)
    if (!unifies(left->parameters[i], right->parameters[i], "array")) return false;
  return unifies(left->result, right->result, "array");
}

bool Checker::holdsFunctionValue(TypePtr type) {
  type = resolve(std::move(type));
  if (type == nullptr) return false;
  if (type->kind == TypeKind::Function) return true;
  if (type->kind == TypeKind::Array) return holdsFunctionValue(type->element);
  return false;
}

TypePtr Checker::inferArray(const reader::ExprPtr &expr, const reader::ArrayExpr &array) {
  TypePtr element = nullType();
  const auto merge = [&](const TypePtr &candidate) {
    const TypePtr current = resolve(element);
    const TypePtr incoming = resolve(candidate);
    if (current == nullptr || current->kind == TypeKind::Null) {
      element = candidate;
      return;
    }
    if (holdsFunctionValue(current) && holdsFunctionValue(incoming)) {
      if (!sameFunctionSignature(current, incoming))
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_ARRAY_FUNCTION_SIGNATURE_MISMATCH,
            typeContext_.show(current) + " and " + typeContext_.show(incoming), expr->range}};
      return;
    }
    if (!unifies(element, candidate, "array"))
      element = typeContext_.unionType({element, candidate});
  };
  for (const auto &elem : array.elements) merge(inferExpression(elem));
  for (const auto &desig : array.designated) merge(inferExpression(desig.value));
  return typeContext_.arrayType(element);
}

TypePtr Checker::resolveTypeAnnotation(const std::shared_ptr<reader::TypeExpr> &annotation) {
  if (annotation == nullptr) return nullptr;

  // Struct type: { fieldType fieldName; ... }
  if (annotation->isStruct) {
    std::vector<std::pair<std::string, TypePtr>> fields;
    fields.reserve(annotation->fields.size());
    for (const auto &[fieldName, fieldAnnotation] : annotation->fields) {
      TypePtr fieldType = resolveTypeAnnotation(fieldAnnotation);
      if (fieldType == nullptr)
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_UNKNOWN_TYPE_NAME, fieldName, annotation->range}};
      fields.emplace_back(fieldName, fieldType);
    }
    return typeContext_.structType(std::move(fields));
  }

  if (annotation->names.empty()) return nullptr;

  // Resolve each name in the annotation
  std::vector<TypePtr> resolved;
  resolved.reserve(annotation->names.size());
  for (std::size_t index = 0; index < annotation->names.size(); ++index) {
    const std::string &name = annotation->names[index];
    TypePtr type = nullptr;

    // Check built-in types
    if (name == "string") type = stringType();
    else if (name == "bool") type = booleanType();
    else if (name == "boolean") type = booleanType();
    else if (name == "byte") type = byteType();
    else if (name == "bytes") type = bytesType();
    else if (name == "int") {
      if (annotation->isIntType)
        type = typeContext_.integerType(annotation->intSize, annotation->intSigned);
      else
        type = typeContext_.integerType(32, true);
    }
    else if (name == "f64") {
      type = numberType(); // f64 resolves to number kind for arithmetic
    }
    else if (name == "usize") type = typeContext_.integerType(64, false);
    else if (name == "va_list") type = numberType(); // va_list is treated as number-sized
    else if (name == "void") type = nullptr; // void is not a valid variable type
    else if (name == "auto")
      type = annotation->pointerDepth[index] > 0 ? freshVariable() : nullptr;
    else if (name == "null") type = nullType();

    // Check type aliases (skip for auto and void, which are intentionally null)
    if (type == nullptr && name != "auto" && name != "void") {
      const auto found = typeAliases_.find(name);
      if (found != typeAliases_.end()) {
        type = found->second;
      } else {
        throw errors::ErrorException{errors::ErrorStatus{
            errors::ErrorCode::TYPE_UNKNOWN_TYPE_NAME, name, annotation->range}};
      }
    }

    // Apply pointer modifier(s) for each level of pointer depth
    for (int depth : annotation->pointerDepth) {
      for (int i = 0; i < depth; ++i) {
        type = pointerType(type);
      }
    }

    // Apply array size modifier to the final member of a union annotation
    if (annotation->isArray && type != nullptr && index + 1 == annotation->names.size()) {
      type = typeContext_.arrayType(type, annotation->arraySize);
    }

    resolved.push_back(type);
  }

  if (resolved.size() == 1) return resolved[0];
  return typeContext_.unionType(std::move(resolved));
}

TypePtr Checker::BuiltinInference::usizeType() {
  return checker_.typeContext_.integerType(64, false);
}

TypePtr Checker::BuiltinInference::bytePointerType() {
  TypeContext &context = checker_.typeContext_;
  return context.pointerType(context.byteType());
}

} // namespace jsmm::types
