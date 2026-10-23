#pragma once

#include "builtins/builtin.hh"
#include "errors/errors.hh"
#include "reader/ast.hh"
#include "types/type_scheme.hh"
#include "types/types.hh"
#include "types/unifier.hh"

#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace jsmm::types {

struct Annotations {
  std::unordered_map<const reader::Expr *, TypePtr> expressionTypes;
  std::unordered_map<const reader::MemberExpr *, std::size_t> structFieldIndices;
  std::unordered_map<const reader::FunctionDeclaration *, TypePtr> functionResults;
  std::unordered_map<std::string, TypePtr> topLevelTypes;
  std::unordered_map<const reader::VariableDeclarator *, TypePtr> declarationTypes;
  std::unordered_map<const reader::VariableDeclarator *, TypePtr> declaredTypes;
  std::unordered_map<const reader::Expr *, TypePtr> argumentTypes;
};

class Checker final : public builtins::InferContext {
public:
  Checker();

  Annotations check(const reader::Program &program);

  TypeContext &context();

  TypePtr freshVariable() override;
  TypePtr stringType() override;
  TypePtr numberType() override;
  TypePtr booleanType() override;
  TypePtr undefinedType() override;
  TypePtr resolveType(TypePtr type) override;
  std::string showType(TypePtr type) override;
  [[noreturn]] void report(const reader::SourceRange &range, const std::string &message) override;
  void requireUnify(TypePtr left, TypePtr right, const reader::SourceRange &range,
                    const std::string &what) override;
  void requireAssignable(TypePtr value, TypePtr target, const reader::SourceRange &range,
                         const std::string &what);
  void requireConcreteType(TypePtr type, const reader::SourceRange &range) override;
  void deferCheck(std::function<void()> check) override;

private:
  struct Binding {
    TypeScheme scheme;
    bool isConst = false;
    bool hoistedVar = false;
  };

  struct Scope {
    std::unordered_map<std::string, Binding> bindings;
    Scope *parent = nullptr;
    bool isFunction = false;
  };

  struct FunctionEntry {
    const reader::FunctionDeclaration *declaration = nullptr;
    reader::SourceRange range{};
    std::vector<TypePtr> parameters;
    TypePtr result;
  };

  struct MemberResolution {
    bool valid = false;
    bool isStruct = false;
    TypePtr elementType;
    std::size_t fieldIndex = 0;
  };

  struct PendingMember {
    const reader::Expr *expression = nullptr;
    const reader::MemberExpr *member = nullptr;
    TypePtr objectType;
    TypePtr resultType;
    reader::SourceRange range{};
  };

  Scope *pushScope(Scope *parent);
  void popScope();
  Binding *lookupBinding(const std::string &name);
  Scope *lookupVarScope();
  std::vector<TypePtr> environmentTypes() const;

  [[noreturn]] void fail(const reader::SourceRange &range, const std::string &message) const;
  [[noreturn]] void fail(const reader::SourceRange &range, const std::string &message,
                         errors::ErrorCode code) const;
  void requireKind(TypePtr type, TypeKind kind, const reader::SourceRange &range,
                   const std::string &what);
  TypePtr annotate(const reader::ExprPtr &expr, TypePtr type);
  TypePtr resolveAnnotation(const reader::TypeExpr &annotation, const reader::SourceRange &range);
  static bool isNullish(TypePtr type);
  static TypePtr unionPayload(TypePtr type);

  void hoistFunctions(const reader::Program &program, std::vector<FunctionEntry> &functions);
  void hoistVarDeclarations(const reader::StatementList &statements, Scope *target);
  void inferFunctionBody(const FunctionEntry &entry, Scope *parentScope);
  void inferStatements(const reader::StatementList &statements);
  void inferStatement(const reader::Statement &statement);
  void inferDeclaration(const reader::VariableDeclaration &declaration);
  TypePtr inferExpression(const reader::ExprPtr &expr);
  TypePtr inferIdentifier(const reader::ExprPtr &expr, const reader::IdentifierExpr &identifier);
  TypePtr inferUnary(const reader::ExprPtr &expr, const reader::UnaryExpr &unary);
  TypePtr inferBinary(const reader::ExprPtr &expr, const reader::BinaryExpr &binary);
  TypePtr inferUpdate(const reader::ExprPtr &expr, const reader::UpdateExpr &update);
  TypePtr inferAssignment(const reader::ExprPtr &expr, const reader::AssignmentExpr &assignment);
  TypePtr inferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member);
  TypePtr inferCall(const reader::ExprPtr &expr, const reader::CallExpr &call);
  TypePtr inferObject(const reader::ExprPtr &expr, const reader::ObjectExpr &object);
  TypePtr inferArray(const reader::ExprPtr &expr, const reader::ArrayExpr &array);
  TypePtr targetType(const reader::ExprPtr &target);
  MemberResolution resolveMember(TypePtr objectType, const reader::MemberExpr &member);
  static bool definitelyReturns(const reader::StatementList &statements);

  void pinType(TypePtr type);
  void runDeferredChecks();
  void collectFreeVariables(TypePtr type, std::vector<TypePtr> &out);
  void collectQuantifiedVariables(std::set<int> &out) const;
  void deferMember(const reader::ExprPtr &expr, const reader::MemberExpr &member,
                   TypePtr objectType, TypePtr resultType);
  void resolvePendingMembers();
  void bindUnresolvedTypes(bool excludeMemberResults);
  void recordTopLevelTypes();
  void finalize();

  TypeContext context_;
  Unifier unifier_;
  std::vector<std::unique_ptr<Scope>> scopePool_;
  Scope rootScope_;
  Scope *currentScope_ = nullptr;
  std::vector<TypePtr> returnTargets_;
  Annotations annotations_;
  std::vector<PendingMember> pendingMembers_;
  std::vector<TypePtr> memberResults_;
  std::vector<TypePtr> pinnedTypes_;
  std::vector<std::function<void()>> deferredChecks_;
  std::vector<TypePtr> concreteTypes_;
  std::unordered_map<const reader::VariableDeclarator *, TypePtr> declarationTypeVars_;
  std::unordered_set<std::string> activeTopLevelVars_;
  bool topLevelSequential_ = false;
  bool functionReturnsValue_ = false;
  bool inFunction_ = false;
  int loopDepth_ = 0;
};

} // namespace jsmm::types
