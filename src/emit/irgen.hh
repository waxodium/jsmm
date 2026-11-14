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
                         const types::TypeContext &context);

  void emitArgument(const reader::ExprPtr &) override;
  void moveResultToArgumentRegister() override;
  void clearResult() override;
  void declareHelper(std::string, std::string) override;
  void useHelper(const std::string &) override;
  void callHelper(const std::string &) override;
  void emitSyscall(const reader::CallExpr &call) override;
  types::TypePtr operandType(const reader::ExprPtr &operand) override;
  void emitNumberResult(double value) override;
  void emitAllocation(const reader::ExprPtr &size) override;
  void emitVoidResult() override;
  void emitFree(const reader::ExprPtr &ptr) override;
  void emitMemcpy(const reader::ExprPtr &dest, const reader::ExprPtr &src,
                  const reader::ExprPtr &count) override;
  void emitMemset(const reader::ExprPtr &dest, const reader::ExprPtr &value,
                  const reader::ExprPtr &count) override;
  void emitMemcmp(const reader::ExprPtr &a, const reader::ExprPtr &b,
                  const reader::ExprPtr &count) override;
  void emitLength(const reader::ExprPtr &operand) override;
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
  };

  struct Scope {
    std::unordered_map<std::string, Slot> slots;
  };

  struct Loop {
    llvm::BasicBlock *breakBlock = nullptr;
    llvm::BasicBlock *continueBlock = nullptr;
  };

  struct Label {
    std::string name;
    llvm::BasicBlock *breakBlock = nullptr;
    llvm::BasicBlock *continueBlock = nullptr;
  };

  types::TypePtr typeOf(const reader::ExprPtr &expression) const;
  types::TypePtr resolve(types::TypePtr type) const;
  bool isNumber(types::TypePtr type) const;
  bool isString(types::TypePtr type) const;
  bool isBytes(types::TypePtr type) const;
  bool isText(types::TypePtr type) const;
  types::TypePtr unionPayload(types::TypePtr type) const;

  llvm::Constant *i64(std::uint64_t value) const;
  llvm::Constant *i32(std::uint64_t value) const;
  EmitValue numberValue(double value) const;
  EmitValue rawValue(llvm::Value *value) const;
  llvm::Value *asRaw(EmitValue value) const;
  llvm::Value *toInt32(llvm::Value *value) const;
  llvm::Value *toInt64(EmitValue value) const;
  llvm::Value *fpToI64(llvm::Value *fpValue) const;
  EmitValue asTyped(llvm::Value *bits, types::TypePtr type) const;

  llvm::Value *bytePointer(llvm::Value *base, std::uint64_t offset) const;
  llvm::Value *buildRestArray(const std::vector<EmitValue> &elements,
                              types::TypePtr elementType);
  llvm::Value *dynamicPointer(llvm::Value *base, llvm::Value *offset) const;
  llvm::Value *allocate(std::uint64_t bytes);
  EmitValue loadSlot(llvm::Value *address, types::TypePtr type) const;
  void storeSlot(llvm::Value *address, EmitValue value, types::TypePtr type) const;
  llvm::Type *storageType(types::TypePtr type) const;
  llvm::Type *floatStorageType(types::TypePtr type) const;
  bool isBfloat16(types::TypePtr type) const;
  std::uint64_t elementSize(types::TypePtr type) const;
  std::uint64_t memberOffset(types::TypePtr structType, std::size_t index) const;

  bool terminated() const;
  llvm::BasicBlock *createBlock(const llvm::Twine &name);
  void beginScope();
  void endScope();
  Slot *lookupSlot(const std::string &name);
  long long allocateSlot();
  llvm::AllocaInst *slotAlloca(long long index, types::TypePtr type);
  llvm::AllocaInst *createEntryAlloca();

  llvm::GlobalVariable *internString(const std::string &text);
  llvm::GlobalVariable *globalFor(const std::string &name, types::TypePtr type);

  llvm::Function *runtimeFunction(llvm::StringRef name, llvm::Type *result,
                                  llvm::ArrayRef<llvm::Type *> parameters);
  llvm::Function *allocFunction();
  llvm::Function *stringWriter();
  llvm::Function *numberWriter();
  llvm::Function *boolWriter();
  llvm::Function *nullWriter();
  llvm::Function *lbracketWriter();
  llvm::Function *separatorWriter();
  llvm::Function *rbracketWriter();
  llvm::Function *moduloFunction();
  llvm::Function *powerFunction();
  llvm::Function *boundsFunction();
  llvm::Function *stringEqualFunction();
  llvm::Function *stringConcatFunction();
  llvm::Function *stringCompareFunction();
  llvm::Function *bytesCopyFunction();
  llvm::Function *memcpyFunction();
  llvm::Function *memsetFunction();
  llvm::Function *memcmpFunction();
  llvm::Function *throwFunction();
  llvm::Function *syscallFunction();

  llvm::Value *stringEquals(llvm::Value *left, llvm::Value *right);
  llvm::Value *emitTruthy(EmitValue value, types::TypePtr type);
  llvm::Value *emitNullish(EmitValue value, types::TypePtr type);
  llvm::Value *emitUnionEquality(EmitValue left, types::TypePtr leftType, EmitValue right,
                                 types::TypePtr rightType);
  long long unionTagFor(types::TypePtr type) const;

  EmitValue emitZeroValue(types::TypePtr type);
  EmitValue emitNullishCell(long long tag);
  EmitValue emitBox(types::TypePtr payloadType, EmitValue value);
  EmitValue coerce(types::TypePtr destination, types::TypePtr source, EmitValue value);
  EmitValue convertStruct(types::TypePtr destination, types::TypePtr source, EmitValue value);
  EmitValue convertArrayToBytes(EmitValue value, types::TypePtr arrayType);
  EmitValue copyText(EmitValue value);
  bool sameLayout(types::TypePtr left, types::TypePtr right) const;

  void generateDeclaration(const reader::VariableDeclaration &declaration);
  void generateStatements(const reader::StatementList &statements);
  void generateStatement(const reader::Statement &statement);
  EmitValue generateExpression(const reader::ExprPtr &expression);
  EmitValue generateUnary(const reader::UnaryExpr &unary);
  EmitValue generateBinary(const reader::BinaryExpr &binary);
  EmitValue generateArithmetic(const reader::BinaryExpr &binary);
  EmitValue generateStringConcat(const reader::BinaryExpr &binary);
  EmitValue generateBitwise(const reader::BinaryExpr &binary);
  EmitValue emitBitwiseOperation(const std::string &op, EmitValue left, EmitValue right);
  EmitValue loadByteElement(llvm::Value *address) const;
  EmitValue loadStringByteElement(llvm::Value *address) const;
  void storeByteElement(llvm::Value *address, EmitValue value) const;
  bool byteElement(const reader::ExprPtr &expression) const;
  EmitValue generateRelational(const reader::BinaryExpr &binary);
  EmitValue generateEquality(const reader::BinaryExpr &binary);
  EmitValue generateLogical(const reader::BinaryExpr &binary);
  EmitValue generateAssignment(const reader::AssignmentExpr &assignment);
  llvm::Value *generateLValueAddress(const reader::ExprPtr &target);
  llvm::Value *generateMemberAddress(const reader::MemberExpr &member,
                                     const reader::SourceRange &range = {});
  EmitValue generateCall(const reader::ExprPtr &expression, const reader::CallExpr &call);

  void emitFormatValue(EmitValue value, types::TypePtr type);
  void emitFormatArray(EmitValue value, types::TypePtr type);
  void emitFormatUnion(EmitValue value, types::TypePtr type);
  std::string typeofName(types::TypePtr type) const;

  // Variadic builtin emission
  void emitVaCount() override;
  void emitVaArg(const reader::CallExpr &call) override;
  EmitValue coerceI64ToType(llvm::Value *i64val, types::TypePtr type);

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
  long long slotCount_ = 0;
  std::vector<llvm::AllocaInst *> slotAllocas_;

  llvm::Function *currentFunction_ = nullptr;
  llvm::BasicBlock *returnBlock_ = nullptr;
  llvm::AllocaInst *returnSlot_ = nullptr;
  types::TypePtr returnType_;
  llvm::Value *vaCountSlot_ = nullptr;     // hidden variadic count parameter slot
  llvm::Value *vaCountValue_ = nullptr;    // direct va_count argument value
  llvm::Value *vaArraySlot_ = nullptr;     // va_array pointer (shadow array)
  std::vector<Loop> loops_;
  std::vector<Label> labels_;
  std::string pendingLoopLabel_;
  bool generatingEntry_ = false;
  EmitValue lastValue_;
  int unionBlockCounter_ = 0;
};

} // namespace jsmm::emit
