#pragma once

#include "reader/ast.hh"
#include "types/types.hh"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jsmm::types {

struct Annotations
{
  std::unordered_map<const reader::Expression *, TypePtr> expressionTypes;
  std::unordered_map<const reader::VariableDeclarator *, TypePtr> declarationTypes;
  std::unordered_map<const reader::Expression *, TypePtr> argumentTypes;
};

class Checker {
public:
  Checker() = default;

  Annotations check(const reader::Program &program);

  const TypeContext &context() const { return typeContext_; }

private:
  struct Scope {
    std::unordered_map<std::string, TypePtr> bindings;
  };

  struct FunctionEntry {
    reader::FunctionDeclaration declaration;
    TypePtr returnType;
    Scope *parentScope = nullptr;
  };

  void hoistFunctions(const reader::Program &program, std::vector<FunctionEntry> &functions);
  void hoistVarDeclarations(const reader::StatementList &statements, Scope *scope);
  void hoistVarDeclarations(const std::unique_ptr<reader::Statement> &stmt, Scope *scope);
  void inferFunctionBody(FunctionEntry &entry, Scope *parentScope);
  void inferStatements(const reader::StatementList &statements);
  void inferDeclaration(const reader::VariableDeclaration &declaration);
  void inferStatement(const reader::Statement &statement);
  TypePtr inferExpression(const reader::ExprPtr &expr);
  TypePtr inferIdentifier(const reader::ExprPtr &expr, const reader::IdentifierExpr &identifier);
  TypePtr inferUnary(const reader::ExprPtr &expr, const reader::UnaryExpr &unary);
  TypePtr inferBinary(const reader::ExprPtr &expr, const reader::BinaryExpr &binary);
  TypePtr inferUpdate(const reader::ExprPtr &expr, const reader::UpdateExpr &update);
  TypePtr inferAssignment(const reader::ExprPtr &expr, const reader::AssignmentExpr &assignment);
  TypePtr targetType(const reader::ExprPtr &target);
  TypePtr inferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member);
  void deferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member, TypePtr objectType,
                   TypePtr result);
  void resolvePendingMembers();
  void bindUnresolvedTypes(bool excludeMemberResults);
  void recordTopLevelTypes();
  void finalize();

  TypePtr numberType() const { return typeContext_.number(); }
  TypePtr stringType() const { return typeContext_.string(); }
  TypePtr booleanType() const { return typeContext_.boolean(); }
  TypePtr nullType() const { return typeContext_.null(); }
  TypePtr undefinedType() const { return typeContext_.undefined(); }
  TypePtr neverType() const { return typeContext_.never(); }
  TypePtr makeFunctionType(TypePtr returnType, std::vector<TypePtr> argumentTypes) {
    return typeContext_.functionType(std::move(argumentTypes), std::move(returnType));
  }

  void insertDeclaredType(const reader::VariableDeclarator *declarator, TypePtr type);
  TypePtr resolve(TypePtr type) const { return typeContext_.resolve(type); }
  TypePtr unionPayload(TypePtr type) const;
  bool isNumber(TypePtr type) const;
  bool unifies(TypePtr left, TypePtr right, std::string_view context) const;
  bool definitelyReturns(const reader::StatementList &statements) const;

  TypePtr inferCall(const reader::ExprPtr &expr, const reader::CallExpr &call);
  TypePtr inferObject(const reader::ExprPtr &expr, const reader::ObjectExpr &object);
  TypePtr inferArray(const reader::ExprPtr &expr, const reader::ArrayExpr &array);

  std::vector<FunctionEntry> functions_;
  std::vector<Scope> scopes_;
  std::vector<std::pair<const reader::Expression *, reader::MemberExpr>> pendingMembers_;
  std::unordered_map<const reader::Expression *, TypePtr> pendingTypeResolutions_;
  TypeContext typeContext_;
  Annotations annotations_;
};

} // namespace jsmm::types
