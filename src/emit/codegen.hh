#pragma once

#include "builtins/builtin.hh"
#include "emit/helpers.hh"
#include "emit/x64/assembly.hh"
#include "reader/ast.hh"
#include "types/checker.hh"
#include "types/types.hh"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace rma::emit {

class CodeGenerator final : public builtins::EmitContext {
public:
  std::string generate(const reader::Program &program, const types::Annotations &annotations,
                       types::TypeContext &context);

  void emitArgument(const reader::ExprPtr &argument) override;
  void moveResultToArgumentRegister() override;
  void clearResult() override;
  void declareHelper(std::string symbol, std::string text) override;
  void useHelper(const std::string &symbol) override;
  void callHelper(const std::string &symbol) override;
  void formatValue(const reader::ExprPtr &expression) override;
  void formatArray(const reader::ExprPtr &expression) override;
  void formatNewline() override;
  void formatByType(const types::TypePtr &type);
  void formatArrayValue(const types::TypePtr &type);

private:
  struct Slot {
    long long offset = 0;
    bool isConst = false;
  };

  struct Scope {
    std::unordered_map<std::string, Slot> slots;
  };

  struct LoopLabels {
    std::string breakLabel;
    std::string continueLabel;
  };

  struct FunctionState {
    std::string returnLabel;
    std::vector<LoopLabels> loops;
    types::TypePtr resultType;
  };

  x64::Assembly &out();
  std::string newLabel(const std::string &prefix);
  std::string internString(const std::string &content);

  void beginScope();
  void endScope();
  Slot *lookupSlot(const std::string &name);
  long long topLevelOffset(const std::string &name);
  bool topLevelIsNumber(const std::string &name);
  long long allocateSlot();
  void releaseSlot(long long offset);
  long long pushTemp();
  void popTemp();
  long long tempOffset(std::size_t depth) const;

  void storeValue(long long offset, types::TypePtr type);
  void loadGp(long long offset, x64::Reg reg);
  void loadXmm(long long offset, x64::Xmm reg);

  types::TypePtr typeOf(const reader::ExprPtr &expr) const;
  bool isNumber(types::TypePtr type) const;
  static std::uint64_t numberBits(const std::string &spelling);

  void generateStatements(const reader::StatementList &statements);
  void generateStatement(const reader::Statement &statement);
  void generateDeclaration(const reader::VariableDeclaration &declaration);
  void generateExpression(const reader::ExprPtr &expr);
  void generateBinary(const reader::BinaryExpr &binary);
  void generateEquality(const reader::BinaryExpr &binary);
  void generateLogical(const reader::BinaryExpr &binary);
  void generateArithmetic(const reader::BinaryExpr &binary);
  void generateBitwise(const reader::BinaryExpr &binary);
  void generateRelational(const reader::BinaryExpr &binary);
  void generateUnary(const reader::UnaryExpr &unary);
  void generateUpdate(const reader::UpdateExpr &update);
  void generateAssignment(const reader::AssignmentExpr &assignment);
  void generateMemberAddress(const reader::MemberExpr &member);
  void generateCall(const reader::ExprPtr &expr, const reader::CallExpr &call);
  void generateArguments(const std::vector<reader::ExprPtr> &arguments);

  std::string generateFunction(const reader::FunctionDeclaration &declaration);
  std::string generateEntry(const reader::Program &program);

  x64::Assembly *out_ = nullptr;
  const types::Annotations *annotations_ = nullptr;
  types::TypeContext *context_ = nullptr;
  HelperRegistry helpers_;
  std::unordered_map<std::string, std::string> functions_;
  std::unordered_map<std::string, std::string> stringLiterals_;
  std::vector<std::pair<std::string, std::vector<std::uint8_t>>> rodataStrings_;
  long long labelCounter_ = 0;
  std::vector<Scope> scopes_;
  long long slotCount_ = 0;
  std::vector<long long> freeSlots_;
  std::vector<long long> temps_;
  FunctionState state_;
  bool generatingEntry_ = false;
  std::unordered_map<std::string, long long> topLevelOffsets_;
  std::unordered_map<std::string, bool> topLevelIsNumber_;
};

} // namespace rma::emit
