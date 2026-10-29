#pragma once

#include "builtins/builtin.hh"
#include "reader/ast.hh"
#include "types/types.hh"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jsmm::types {

struct Annotations {
  std::unordered_map<const reader::Expression *, TypePtr> expressionTypes;
  std::unordered_map<const reader::VariableDeclarator *, TypePtr> declarationTypes;
  std::unordered_map<const reader::Expression *, TypePtr> argumentTypes;
  std::unordered_map<std::string, TypePtr> functionTypes;
};

class Checker {
public:
  Checker() = default;

  Annotations check(const reader::Program &program);

  const TypeContext &context() const { return typeContext_; }

private:
  struct Scope {
    std::unordered_map<std::string, TypePtr> bindings;
    std::unordered_set<std::string> constants;
  };

  struct FunctionEntry {
    reader::FunctionDeclaration declaration;
    TypePtr returnType;
    std::vector<TypePtr> parameterTypes;
    Scope *parentScope = nullptr;
  };

  class BuiltinInference final : public builtins::InferContext {
  public:
    explicit BuiltinInference(Checker &checker) : checker_(checker) {}

    TypePtr freshVariable() override;
    TypePtr stringType() override;
    TypePtr numberType() override;
    TypePtr booleanType() override;
    TypePtr undefinedType() override;
    TypePtr resolveType(TypePtr type) override;
    [[nodiscard]] std::string showType(TypePtr type) override;
    [[noreturn]] void report(const reader::SourceRange &range,
                             const std::string &message) override;
    void requireUnify(TypePtr left, TypePtr right, const reader::SourceRange &range,
                      const std::string &what) override;
    void requireConcreteType(TypePtr type, const reader::SourceRange &range) override;
    void deferCheck(std::function<void()> check) override;

  private:
    Checker &checker_;
  };

  void hoistFunctions(const reader::Program &program, std::vector<FunctionEntry> &functions);
  void hoistVarDeclarations(const reader::StatementList &statements, Scope *scope);
  void hoistVarDeclarations(const std::unique_ptr<reader::Statement> &stmt, Scope *scope);
  void inferFunctionBody(FunctionEntry &entry, reader::BlockStatement *body, Scope *parentScope);
  void inferStatements(const reader::StatementList &statements);
  void inferDeclaration(const reader::VariableDeclaration &declaration);
  void inferStatement(const reader::Statement &statement);
  TypePtr inferExpression(const reader::ExprPtr &expr);
  TypePtr inferIdentifier(const reader::ExprPtr &expr, const reader::IdentifierExpr &identifier);
  TypePtr inferUnary(const reader::ExprPtr &expr, const reader::UnaryExpr &unary);
  TypePtr inferAddressOf(const reader::ExprPtr &expr, const reader::AddressOfExpr &addrOf);
  TypePtr inferDereference(const reader::ExprPtr &expr, const reader::DereferenceExpr &deref);
  TypePtr inferBinary(const reader::ExprPtr &expr, const reader::BinaryExpr &binary);
  TypePtr inferUpdate(const reader::ExprPtr &expr, const reader::UpdateExpr &update);
  TypePtr inferAssignment(const reader::ExprPtr &expr, const reader::AssignmentExpr &assignment);
  TypePtr targetType(const reader::ExprPtr &target);
  TypePtr inferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member);
  TypePtr memberResultType(const reader::MemberExpr &member, const reader::Expression &expression,
                           TypePtr objectType);
  void deferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member,
                   TypePtr objectType, TypePtr result);
  void resolvePendingMembers();
  void bindUnresolvedTypes(bool excludeMemberResults);
  void recordTopLevelTypes();
  void finalize();

  TypePtr numberType() const { return typeContext_.number(); }
  TypePtr stringType() const { return typeContext_.string(); }
  TypePtr booleanType() const { return typeContext_.boolean(); }
  TypePtr byteType() const { return typeContext_.byteType(); }
  TypePtr bytesType() const { return typeContext_.bytesType(); }
  TypePtr nullType() const { return typeContext_.nullType(); }
  TypePtr neverType() const { return typeContext_.never(); }
  TypePtr pointerType(TypePtr pointed) { return typeContext_.pointerType(std::move(pointed)); }
  TypePtr makeFunctionType(TypePtr returnType, std::vector<TypePtr> argumentTypes) {
    return typeContext_.functionType(std::move(argumentTypes), std::move(returnType));
  }
  TypePtr freshVariable();

  void insertDeclaredType(const reader::VariableDeclarator *declarator, TypePtr type);
  TypePtr resolve(TypePtr type) const { return typeContext_.resolve(type); }
  TypePtr unionPayload(TypePtr type) const;
  bool isNumber(TypePtr type) const;
  bool isString(TypePtr type) const;
  bool requireNumeric(TypePtr type);
  bool unifies(TypePtr left, TypePtr right, std::string_view context);
  bool definitelyReturns(const reader::StatementList &statements) const;
  bool isAddressable(const reader::ExprPtr &expr) const;

  TypePtr inferCall(const reader::ExprPtr &expr, const reader::CallExpr &call);
  TypePtr inferObject(const reader::ExprPtr &expr, const reader::ObjectExpr &object);
  TypePtr inferArray(const reader::ExprPtr &expr, const reader::ArrayExpr &array);
  TypePtr resolveTypeAnnotation(const std::shared_ptr<reader::TypeExpr> &annotation);

  std::vector<FunctionEntry> functions_;
  std::vector<Scope> scopes_;
  std::vector<FunctionEntry *> currentFunctions_;
  std::vector<std::pair<std::string, bool>> labels_;
  int loopDepth_ = 0;
  std::vector<std::vector<TypePtr>> returnTypeStack_;
  std::vector<std::pair<const reader::Expression *, reader::MemberExpr>> pendingMembers_;
  std::unordered_map<const reader::Expression *, TypePtr> pendingTypeResolutions_;
  std::vector<std::function<void()>> deferredChecks_;
  std::vector<TypePtr> inferenceVariables_;
  BuiltinInference builtinInference_{*this};
  TypeContext typeContext_;
  Annotations annotations_;
  std::unordered_map<std::string, TypePtr> typeAliases_;
};

} // namespace jsmm::types
