#pragma once

#include "builtins/builtin.hh"
#include "reader/ast.hh"
#include "types/checker.hh"
#include "types/types.hh"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace jsmm::emit {

struct LoweredModule {
  std::shared_ptr<llvm::LLVMContext> context;
  std::unique_ptr<llvm::Module> module;
};

class IrGenerator final : public builtins::EmitContext {
public:
  LoweredModule generate(const reader::Program &program, const types::Annotations &annotations,
                         types::TypeContext &context);

  void emitArgument(const reader::ExprPtr &) override;
  void moveResultToArgumentRegister() override;
  void clearResult() override;
  void declareHelper(std::string, std::string) override;
  void useHelper(const std::string &) override;
  void callHelper(const std::string &) override;
  void formatValue(const reader::ExprPtr &expression) override;
  void formatArray(const reader::ExprPtr &expression) override;
  void formatNewline() override;

private:
  struct EmitValue {
    llvm::Value *value = nullptr;
    bool number = false;
  };

  struct Slot {
    llvm::Value *address = nullptr;
    bool isConst = false;
  };

  struct Scope {
    std::unordered_map<std::string, Slot> slots;
  };

  struct Loop {
    llvm::BasicBlock *breakBlock = nullptr;
    llvm::BasicBlock *continueBlock = nullptr;
  };

  types::TypePtr typeOf(const reader::ExprPtr &expression) const;
  types::TypePtr resolve(types::TypePtr type) const;
  bool isNumber(types::TypePtr type) const;
  types::TypePtr unionPayload(types::TypePtr type) const;

  llvm::Constant *i64(std::uint64_t value) const;
  llvm::Constant *i32(std::uint64_t value) const;
  EmitValue numberValue(double value) const;
  EmitValue rawValue(llvm::Value *value) const;
  llvm::Value *asRaw(EmitValue value) const;
  llvm::Value *toInt32(llvm::Value *value) const;
  EmitValue asTyped(llvm::Value *bits, types::TypePtr type) const;

  llvm::Value *bytePointer(llvm::Value *base, std::uint64_t offset) const;
  llvm::Value *dynamicPointer(llvm::Value *base, llvm::Value *offset) const;
  llvm::Value *allocate(std::uint64_t bytes);
  EmitValue loadSlot(llvm::Value *address, types::TypePtr type) const;
  void storeSlot(llvm::Value *address, EmitValue value) const;

  bool terminated() const;
  llvm::BasicBlock *createBlock(const llvm::Twine &name);
  void beginScope();
  void endScope();
  Slot *lookupSlot(const std::string &name);
  long long allocateSlot();
  void releaseSlot(long long index);
  long long pushTemp();
  void popTemp();
  llvm::AllocaInst *slotAlloca(long long index);
  llvm::AllocaInst *createEntryAlloca();

  llvm::GlobalVariable *internString(const std::string &text);
  llvm::GlobalVariable *globalFor(const std::string &name);

  llvm::Function *runtimeFunction(llvm::StringRef name, llvm::Type *result,
                                  llvm::ArrayRef<llvm::Type *> parameters);
  llvm::Function *allocFunction();
  llvm::Function *stringWriter();
  llvm::Function *numberWriter();
  llvm::Function *boolWriter();
  llvm::Function *nullWriter();
  llvm::Function *undefinedWriter();
  llvm::Function *lbracketWriter();
  llvm::Function *separatorWriter();
  llvm::Function *rbracketWriter();
  llvm::Function *moduloFunction();
  llvm::Function *powerFunction();
  llvm::Function *boundsFunction();

  EmitValue emitZeroValue(types::TypePtr type);
  EmitValue emitNullishCell(long long tag);
  EmitValue emitBox(types::TypePtr payloadType, EmitValue value);
  EmitValue coerce(types::TypePtr destination, types::TypePtr source, EmitValue value);
  EmitValue convertStruct(types::TypePtr destination, types::TypePtr source, EmitValue value);
  bool sameLayout(types::TypePtr left, types::TypePtr right) const;

  void collectVarDeclarations(const reader::StatementList &statements,
                              std::vector<const reader::VariableDeclarator *> &out) const;
  void hoistVarSlots(const reader::StatementList &statements);
  void generateDeclaration(const reader::VariableDeclaration &declaration);
  void generateStatements(const reader::StatementList &statements);
  void generateStatement(const reader::Statement &statement);
  EmitValue generateExpression(const reader::ExprPtr &expression);
  EmitValue generateUnary(const reader::UnaryExpr &unary);
  EmitValue generateBinary(const reader::BinaryExpr &binary);
  EmitValue generateArithmetic(const reader::BinaryExpr &binary);
  EmitValue generateBitwise(const reader::BinaryExpr &binary);
  EmitValue generateRelational(const reader::BinaryExpr &binary);
  EmitValue generateEquality(const reader::BinaryExpr &binary);
  EmitValue generateLogical(const reader::BinaryExpr &binary);
  EmitValue generateUpdate(const reader::UpdateExpr &update);
  EmitValue generateAssignment(const reader::AssignmentExpr &assignment);
  llvm::Value *generateMemberAddress(const reader::MemberExpr &member);
  EmitValue generateCall(const reader::ExprPtr &expression, const reader::CallExpr &call);

  void emitFormatValue(EmitValue value, types::TypePtr type);
  void emitFormatArray(EmitValue value, types::TypePtr type);
  void emitFormatUnion(EmitValue value, types::TypePtr type);
  std::string typeofName(types::TypePtr type) const;

  void generateFunction(const reader::FunctionDeclaration &declaration, llvm::Function *function);
  void generateEntry(const reader::Program &program);

  llvm::LLVMContext *context_ = nullptr;
  llvm::Module *module_ = nullptr;
  std::unique_ptr<llvm::IRBuilder<>> builder_;
  const types::Annotations *annotations_ = nullptr;
  types::TypeContext *types_ = nullptr;
  llvm::Type *doubleType_ = nullptr;
  llvm::Type *i64Type_ = nullptr;
  llvm::Type *i32Type_ = nullptr;
  llvm::Type *i8Type_ = nullptr;
  llvm::Type *ptrType_ = nullptr;
  llvm::Type *voidType_ = nullptr;

  std::unordered_map<std::string, llvm::GlobalVariable *> globals_;
  std::unordered_map<std::string, llvm::Function *> functions_;
  std::unordered_map<std::string, llvm::GlobalVariable *> strings_;
  std::unordered_map<std::string, llvm::Function *> runtime_;
  llvm::GlobalVariable *emptyString_ = nullptr;

  std::vector<Scope> scopes_;
  int varTargetScopeIndex_ = -1;
  long long slotCount_ = 0;
  std::vector<long long> freeSlots_;
  std::vector<long long> temps_;
  std::vector<llvm::AllocaInst *> slotAllocas_;

  llvm::Function *currentFunction_ = nullptr;
  llvm::BasicBlock *returnBlock_ = nullptr;
  llvm::AllocaInst *returnSlot_ = nullptr;
  types::TypePtr returnType_;
  std::vector<Loop> loops_;
  bool generatingEntry_ = false;
  EmitValue lastValue_;
};

}
