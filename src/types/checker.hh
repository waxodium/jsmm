#pragma once

#include "reader/ast.hh"
#include "builtins/builtin.hh"
#include "types/type_scheme.hh"
#include "types/types.hh"
#include "types/unifier.hh"

#include <memory>
#include <unordered_map>
#include <vector>

namespace rma::types {

struct Annotations {
  std::unordered_map<const reader::Expr *, TypePtr> expressionTypes;
  std::unordered_map<const reader::MemberExpr *, std::size_t> structFieldIndices;
  std::unordered_map<const reader::FunctionDeclaration *, TypePtr> functionResults;
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
  void requireUnify(TypePtr left, TypePtr right, const reader::SourceRange &range,
                    const std::string &what) override;

private:
  struct Binding {
    TypeScheme scheme;
    bool isConst = false;
  };

  struct Scope {
    std::unordered_map<std::string, Binding> bindings;
    Scope *parent = nullptr;
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

  Scope *pushScope(Scope *parent);
  void popScope();
  Binding *lookupBinding(const std::string &name);
  std::vector<TypePtr> environmentTypes() const;

  [[noreturn]] void fail(const reader::SourceRange &range, const std::string &message) const;
  void requireKind(TypePtr type, TypeKind kind, const reader::SourceRange &range,
                   const std::string &what);
  TypePtr annotate(const reader::ExprPtr &expr, TypePtr type);

  void hoistFunctions(const reader::Program &program, std::vector<FunctionEntry> &functions);
  void inferFunctionBody(const FunctionEntry &entry);
  void inferStatements(const reader::StatementList &statements);
  void inferStatement(const reader::Statement &statement);
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

  TypeContext context_;
  Unifier unifier_;
  std::vector<std::unique_ptr<Scope>> scopePool_;
  Scope rootScope_;
  Scope *currentScope_ = nullptr;
  std::vector<TypePtr> returnTargets_;
  Annotations annotations_;
  bool inFunction_ = false;
  int loopDepth_ = 0;
};

}
