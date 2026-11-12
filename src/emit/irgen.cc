#include "emit/irgen.hh"

#include "errors/errors.hh"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Verifier.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace jsmm::emit {
namespace {

using types::TypeKind;
using types::TypePtr;

double parseNumber(const std::string &spelling) {
  if (spelling.size() > 2 && spelling[0] == '0' && (spelling[1] == 'x' || spelling[1] == 'X'))
    return static_cast<double>(std::strtoull(spelling.c_str() + 2, nullptr, 16));
  if (spelling.size() > 2 && spelling[0] == '0' && (spelling[1] == 'b' || spelling[1] == 'B')) {
    unsigned long long result = 0;
    for (std::size_t index = 2; index < spelling.size(); ++index)
      result = result * 2ULL + static_cast<unsigned long long>(spelling[index] - '0');
    return static_cast<double>(result);
  }
  if (spelling.size() > 2 && spelling[0] == '0' && (spelling[1] == 'o' || spelling[1] == 'O')) {
    unsigned long long result = 0;
    for (std::size_t index = 2; index < spelling.size(); ++index)
      result = result * 8ULL + static_cast<unsigned long long>(spelling[index] - '0');
    return static_cast<double>(result);
  }
  return std::strtod(spelling.c_str(), nullptr);
}

constexpr long long unionTagNull = 0;
constexpr long long unionTagNumber = 1;
constexpr long long unionTagString = 2;
constexpr long long unionTagBoolean = 3;
constexpr long long unionTagPayload = 4;

} // namespace

types::TypePtr IrGenerator::typeOf(const reader::ExprPtr &expression) const {
  return annotations_->expressionTypes.at(expression.get());
}

types::TypePtr IrGenerator::resolve(types::TypePtr type) const {
  return types_->resolve(std::move(type));
}

bool IrGenerator::isNumber(types::TypePtr type) const {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return false;
  return resolved->kind == TypeKind::Number || resolved->kind == TypeKind::Byte ||
         resolved->kind == TypeKind::Integer;
}

bool IrGenerator::isString(types::TypePtr type) const {
  const TypePtr resolved = resolve(std::move(type));
  return resolved != nullptr && resolved->kind == TypeKind::String;
}

bool IrGenerator::isBytes(types::TypePtr type) const {
  const TypePtr resolved = resolve(std::move(type));
  return resolved != nullptr && resolved->kind == TypeKind::Bytes;
}

bool IrGenerator::isText(types::TypePtr type) const { return isString(type) || isBytes(type); }

types::TypePtr IrGenerator::unionPayload(types::TypePtr type) const {
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

std::string IrGenerator::typeofName(types::TypePtr type) const {
  type = resolve(type);
  if (type == nullptr) return "null";
  switch (type->kind) {
  case TypeKind::Number: return "f64";
  case TypeKind::Integer: return types_->show(type);
  case TypeKind::String: return "string";
  case TypeKind::Boolean: return "boolean";
  case TypeKind::Byte: return "byte";
  case TypeKind::Bytes: return "bytes";
  case TypeKind::Null: return "null";
  case TypeKind::Never: return "null";
  case TypeKind::Function: return "function";
  case TypeKind::Pointer: return "pointer";
  case TypeKind::Struct:
  case TypeKind::Array:
  case TypeKind::Union: return "object";
  case TypeKind::Variable: return "null";
  }
  return "null";
}

llvm::Constant *IrGenerator::i64(std::uint64_t value) const {
  return llvm::ConstantInt::get(i64Type_, value, false);
}

llvm::Constant *IrGenerator::i32(std::uint64_t value) const {
  return llvm::ConstantInt::get(i32Type_, value, false);
}

IrGenerator::EmitValue IrGenerator::numberValue(double value) const {
  return EmitValue{llvm::ConstantFP::get(doubleType_, value), true};
}

IrGenerator::EmitValue IrGenerator::rawValue(llvm::Value *value) const {
  return EmitValue{value, false};
}

llvm::Value *IrGenerator::asRaw(EmitValue value) const {
  if (!value.number) return value.value;
  return builder_->CreateBitCast(value.value, i64Type_);
}

llvm::Value *IrGenerator::toInt64(EmitValue value) const {
  if (!value.number) return asRaw(value);
  return builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat, {i64Type_, doubleType_},
                                   {value.value});
}

llvm::Value *IrGenerator::toInt32(llvm::Value *value) const {
  llvm::Value *wide =
      builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat, {i64Type_, doubleType_}, {value});
  return builder_->CreateTrunc(wide, i32Type_);
}

IrGenerator::EmitValue IrGenerator::asTyped(llvm::Value *bits, types::TypePtr type) const {
  if (isNumber(type)) return EmitValue{builder_->CreateBitCast(bits, doubleType_), true};
  return EmitValue{bits, false};
}

llvm::Value *IrGenerator::bytePointer(llvm::Value *base, std::uint64_t offset) const {
  llvm::Value *pointer = builder_->CreateIntToPtr(base, ptrType_);
  if (offset == 0) return pointer;
  return builder_->CreateGEP(i8Type_, pointer, i64(offset));
}

llvm::Value *IrGenerator::dynamicPointer(llvm::Value *base, llvm::Value *offset) const {
  return builder_->CreateGEP(i8Type_, builder_->CreateIntToPtr(base, ptrType_), offset);
}

llvm::Value *IrGenerator::allocate(std::uint64_t bytes) {
  return builder_->CreateCall(allocFunction(), {i64(bytes)});
}

llvm::Value *IrGenerator::buildRestArray(const std::vector<EmitValue> &elements,
                                         types::TypePtr elementType) {
  const std::uint64_t stride = elementSize(elementType);
  llvm::Value *base = allocate(8 + stride * elements.size());
  builder_->CreateStore(i64(elements.size()), bytePointer(base, 0));
  for (std::size_t index = 0; index < elements.size(); ++index)
    storeSlot(bytePointer(base, 8 + stride * index), elements[index], elementType);
  return base;
}

llvm::Type *IrGenerator::storageType(types::TypePtr type) const {
  const types::TypeLayout layout = types::layoutOf(type);
  if (!layout.defined) return i64Type_;
  if (layout.size != 1 && layout.size != 2 && layout.size != 4 && layout.size != 8) return i64Type_;
  return builder_->getIntNTy(static_cast<unsigned>(layout.size * 8));
}

std::uint64_t IrGenerator::elementSize(types::TypePtr type) const {
  const types::TypeLayout layout = types::layoutOf(type);
  if (!layout.defined || layout.size == 0) return 8;
  return layout.size;
}

std::uint64_t IrGenerator::memberOffset(types::TypePtr structType, std::size_t index) const {
  return types::fieldOffset(structType, index);
}

IrGenerator::EmitValue IrGenerator::loadSlot(llvm::Value *address, types::TypePtr type) const {
  const TypePtr resolved = resolve(type);
  const types::TypeLayout layout = types::layoutOf(resolved);
  if (!layout.defined ||
      (layout.size != 1 && layout.size != 2 && layout.size != 4 && layout.size != 8)) {
    llvm::Value *bits = builder_->CreateLoad(i64Type_, address);
    if (isNumber(resolved)) return EmitValue{builder_->CreateBitCast(bits, doubleType_), true};
    return EmitValue{bits, false};
  }
  llvm::Value *loaded = builder_->CreateLoad(storageType(resolved), address);
  llvm::Value *bits = loaded;
  if (layout.size < 8) {
    const bool signExtend =
        resolved != nullptr && (resolved->kind == TypeKind::Byte ||
                                (resolved->kind == TypeKind::Integer && resolved->isSigned));
    bits = signExtend ? builder_->CreateSExt(loaded, i64Type_)
                      : builder_->CreateZExt(loaded, i64Type_);
  }
  if (resolved != nullptr && resolved->kind == TypeKind::Number)
    return EmitValue{builder_->CreateBitCast(bits, doubleType_), true};
  if (!isNumber(resolved)) return EmitValue{bits, false};
  const bool signedNumber = resolved->kind == TypeKind::Byte ||
                            (resolved->kind == TypeKind::Integer && resolved->isSigned);
  return EmitValue{signedNumber ? builder_->CreateSIToFP(bits, doubleType_)
                                : builder_->CreateUIToFP(bits, doubleType_),
                   true};
}

void IrGenerator::storeSlot(llvm::Value *address, EmitValue value, types::TypePtr type) const {
  const TypePtr resolved = resolve(type);
  const types::TypeLayout layout = types::layoutOf(resolved);
  if (!layout.defined ||
      (layout.size != 1 && layout.size != 2 && layout.size != 4 && layout.size != 8)) {
    builder_->CreateStore(asRaw(value), address);
    return;
  }
  llvm::Value *bits = nullptr;
  if (isNumber(resolved)) {
    if (resolved != nullptr && resolved->kind == TypeKind::Number) {
      bits = builder_->CreateBitCast(value.value, i64Type_);
    } else {
      const bool signedNumber = resolved->kind == TypeKind::Byte ||
                                (resolved->kind == TypeKind::Integer && resolved->isSigned);
      bits = signedNumber
                 ? builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat, {i64Type_, doubleType_},
                                             {value.value})
                 : builder_->CreateIntrinsic(llvm::Intrinsic::fptoui_sat, {i64Type_, doubleType_},
                                             {value.value});
    }
  } else {
    bits = asRaw(value);
  }
  if (layout.size < 8) bits = builder_->CreateTrunc(bits, storageType(resolved));
  builder_->CreateStore(bits, address);
}

bool IrGenerator::terminated() const {
  const llvm::BasicBlock *block = builder_->GetInsertBlock();
  return block != nullptr && block->hasTerminator();
}

llvm::BasicBlock *IrGenerator::createBlock(const llvm::Twine &name) {
  return llvm::BasicBlock::Create(*context_, name, currentFunction_);
}

void IrGenerator::beginScope() { scopes_.emplace_back(); }

void IrGenerator::endScope() { scopes_.pop_back(); }

IrGenerator::Slot *IrGenerator::lookupSlot(const std::string &name) {
  for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope) {
    const auto found = scope->slots.find(name);
    if (found != scope->slots.end()) return &found->second;
  }
  return nullptr;
}

long long IrGenerator::allocateSlot() { return slotCount_++; }

llvm::AllocaInst *IrGenerator::slotAlloca(long long index, types::TypePtr type) {
  if (static_cast<std::size_t>(index) < slotAllocas_.size() && slotAllocas_[index] != nullptr)
    return slotAllocas_[index];
  if (slotAllocas_.size() <= static_cast<std::size_t>(index))
    slotAllocas_.resize(index + 1, nullptr);
  llvm::BasicBlock &entry = currentFunction_->getEntryBlock();
  llvm::IRBuilder<> allocator(&entry, entry.begin());
  const types::TypeLayout layout = types::layoutOf(type);
  llvm::AllocaInst *alloca = allocator.CreateAlloca(storageType(type));
  std::uint64_t alignment = layout.defined ? layout.alignment : 8;
  if (alignment > 8) alignment = 8;
  if (alignment == 0) alignment = 1;
  alloca->setAlignment(llvm::Align(alignment));
  slotAllocas_[index] = alloca;
  return alloca;
}

llvm::AllocaInst *IrGenerator::createEntryAlloca() {
  llvm::BasicBlock &entry = currentFunction_->getEntryBlock();
  llvm::IRBuilder<> allocator(&entry, entry.begin());
  llvm::AllocaInst *alloca = allocator.CreateAlloca(i64Type_);
  alloca->setAlignment(llvm::Align(8));
  return alloca;
}

llvm::GlobalVariable *IrGenerator::internString(const std::string &text) {
  const auto found = strings_.find(text);
  if (found != strings_.end()) return found->second;
  llvm::ArrayType *arrayType = llvm::ArrayType::get(i8Type_, text.size());
  llvm::StructType *structType = llvm::StructType::get(*context_, {i64Type_, arrayType});
  std::vector<std::uint8_t> bytes(text.begin(), text.end());
  llvm::Constant *data = llvm::ConstantDataArray::get(*context_, bytes);
  std::vector<llvm::Constant *> elements{i64(text.size()), data};
  llvm::Constant *initializer = llvm::ConstantStruct::get(structType, elements);
  auto *global =
      new llvm::GlobalVariable(*module_, structType, true, llvm::GlobalValue::InternalLinkage,
                               initializer, "jsmm.str." + std::to_string(strings_.size()));
  global->setAlignment(llvm::Align(8));
  global->setDSOLocal(true);
  strings_[text] = global;
  return global;
}

llvm::GlobalVariable *IrGenerator::globalFor(const std::string &name, types::TypePtr type) {
  const auto found = globals_.find(name);
  if (found != globals_.end()) return found->second;
  const types::TypeLayout layout = types::layoutOf(type);
  llvm::Type *storage = storageType(type);
  std::uint64_t alignment = layout.defined ? layout.alignment : 8;
  if (alignment > 8) alignment = 8;
  if (alignment == 0) alignment = 1;
  auto *global = new llvm::GlobalVariable(*module_, storage, false,
                                          llvm::GlobalValue::InternalLinkage,
                                          llvm::ConstantInt::get(storage, 0, false),
                                          "jsmm.g." + name);
  global->setAlignment(llvm::Align(alignment));
  global->setDSOLocal(true);
  globals_[name] = global;
  return global;
}

llvm::Function *IrGenerator::runtimeFunction(llvm::StringRef name, llvm::Type *result,
                                             llvm::ArrayRef<llvm::Type *> parameters) {
  const auto found = runtime_.find(name.str());
  if (found != runtime_.end()) return found->second;
  llvm::FunctionType *type = llvm::FunctionType::get(result, parameters, false);
  llvm::Function *function =
      llvm::Function::Create(type, llvm::Function::ExternalLinkage, name, *module_);
  function->setDSOLocal(true);
  runtime_[name.str()] = function;
  return function;
}

llvm::Function *IrGenerator::allocFunction() {
  return runtimeFunction("jsmm_alloc", i64Type_, {i64Type_});
}

llvm::Function *IrGenerator::stringWriter() {
  return runtimeFunction("jsmm_cat_str", voidType_, {i64Type_});
}

llvm::Function *IrGenerator::numberWriter() {
  return runtimeFunction("jsmm_write_number", voidType_, {doubleType_});
}

llvm::Function *IrGenerator::boolWriter() {
  return runtimeFunction("jsmm_write_bool", voidType_, {i64Type_});
}

llvm::Function *IrGenerator::nullWriter() {
  return runtimeFunction("jsmm_write_null", voidType_, {});
}

llvm::Function *IrGenerator::lbracketWriter() {
  return runtimeFunction("jsmm_write_lbracket", voidType_, {});
}

llvm::Function *IrGenerator::separatorWriter() {
  return runtimeFunction("jsmm_write_separator", voidType_, {});
}

llvm::Function *IrGenerator::rbracketWriter() {
  return runtimeFunction("jsmm_write_rbracket", voidType_, {});
}

llvm::Function *IrGenerator::moduloFunction() {
  return runtimeFunction("jsmm_mod", doubleType_, {doubleType_, doubleType_});
}

llvm::Function *IrGenerator::powerFunction() {
  return runtimeFunction("jsmm_pow", doubleType_, {doubleType_, doubleType_});
}

llvm::Function *IrGenerator::boundsFunction() { return runtimeFunction("jsmm_oob", voidType_, {}); }

llvm::Function *IrGenerator::stringEqualFunction() {
  return runtimeFunction("jsmm_str_eq", i64Type_, {i64Type_, i64Type_});
}

llvm::Function *IrGenerator::stringConcatFunction() {
  return runtimeFunction("jsmm_str_cat", i64Type_, {i64Type_, i64Type_});
}

llvm::Function *IrGenerator::stringCompareFunction() {
  return runtimeFunction("jsmm_str_cmp", i64Type_, {i64Type_, i64Type_});
}

llvm::Function *IrGenerator::bytesCopyFunction() {
  return runtimeFunction("jsmm_bytes_copy", i64Type_, {i64Type_});
}

llvm::Function *IrGenerator::memcpyFunction() {
  return runtimeFunction("jsmm_memcpy", i64Type_, {i64Type_, i64Type_, i64Type_});
}

llvm::Function *IrGenerator::memsetFunction() {
  return runtimeFunction("jsmm_memset", i64Type_, {i64Type_, i64Type_, i64Type_});
}

llvm::Function *IrGenerator::memcmpFunction() {
  return runtimeFunction("jsmm_memcmp", i64Type_, {i64Type_, i64Type_, i64Type_});
}

llvm::Function *IrGenerator::throwFunction() {
  return runtimeFunction("jsmm_throw", voidType_, {});
}

llvm::Function *IrGenerator::syscallFunction() {
  return runtimeFunction("jsmm_syscall", i64Type_,
                         {i64Type_, i64Type_, i64Type_, i64Type_, i64Type_, i64Type_, i64Type_});
}

void IrGenerator::emitSyscall(const reader::CallExpr &call) {
  std::vector<llvm::Value *> values;
  values.reserve(7);
  for (const auto &argument : call.arguments) {
    const EmitValue value = generateExpression(argument);
    values.push_back(value.number ? toInt64(value) : asRaw(value));
  }
  while (values.size() < 7) values.push_back(i64(0));
  llvm::Value *result =
      builder_->CreateCall(syscallFunction(), {values[0], values[1], values[2], values[3],
                                               values[4], values[5], values[6]});
  lastValue_ = EmitValue{builder_->CreateSIToFP(result, doubleType_), true};
}

llvm::Value *IrGenerator::stringEquals(llvm::Value *left, llvm::Value *right) {
  llvm::Value *result = builder_->CreateCall(stringEqualFunction(), {left, right});
  return builder_->CreateICmpNE(result, i64(0));
}

long long IrGenerator::unionTagFor(types::TypePtr type) const {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return unionTagPayload;
  switch (resolved->kind) {
  case TypeKind::Null: return unionTagNull;
  case TypeKind::Number:
  case TypeKind::Byte:
  case TypeKind::Integer: return unionTagNumber;
  case TypeKind::String: return unionTagString;
  case TypeKind::Boolean: return unionTagBoolean;
  default: return unionTagPayload;
  }
}

llvm::Value *IrGenerator::emitTruthy(EmitValue value, types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  llvm::Value *zeroFloat = llvm::ConstantFP::get(doubleType_, 0.0);
  if (resolved == nullptr) return builder_->CreateICmpNE(asRaw(value), i64(0));
  switch (resolved->kind) {
  case TypeKind::Null:
  case TypeKind::Never: return llvm::ConstantInt::getFalse(*context_);
  case TypeKind::Number:
  case TypeKind::Byte:
  case TypeKind::Integer: return builder_->CreateFCmpONE(value.value, zeroFloat);
  case TypeKind::String: {
    llvm::Value *length = builder_->CreateLoad(i64Type_, bytePointer(asRaw(value), 0));
    return builder_->CreateICmpNE(length, i64(0));
  }
  case TypeKind::Union: {
    llvm::Value *base = asRaw(value);
    llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
    llvm::Value *payload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
    llvm::Value *isNumber = builder_->CreateICmpEQ(tag, i64(unionTagNumber));
    llvm::Value *isString = builder_->CreateICmpEQ(tag, i64(unionTagString));
    llvm::Value *stringPointer =
        builder_->CreateSelect(isString, payload, builder_->CreatePtrToInt(emptyString_, i64Type_));
    llvm::Value *length =
        builder_->CreateLoad(i64Type_, builder_->CreateIntToPtr(stringPointer, ptrType_));
    llvm::Value *stringTruthy = builder_->CreateICmpNE(length, i64(0));
    llvm::Value *numberTruthy =
        builder_->CreateFCmpONE(builder_->CreateBitCast(payload, doubleType_), zeroFloat);
    llvm::Value *payloadTruthy = builder_->CreateICmpNE(payload, i64(0));
    llvm::Value *truthy = builder_->CreateSelect(
        isNumber, numberTruthy, builder_->CreateSelect(isString, stringTruthy, payloadTruthy));
    return builder_->CreateAnd(builder_->CreateICmpNE(tag, i64(unionTagNull)), truthy);
  }
  default: return builder_->CreateICmpNE(asRaw(value), i64(0));
  }
}

llvm::Value *IrGenerator::emitNullish(EmitValue value, types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return llvm::ConstantInt::getFalse(*context_);
  switch (resolved->kind) {
  case TypeKind::Null:
  case TypeKind::Never: return llvm::ConstantInt::getTrue(*context_);
  case TypeKind::Union: {
    llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(asRaw(value), 0));
    return builder_->CreateICmpEQ(tag, i64(unionTagNull));
  }
  default: return llvm::ConstantInt::getFalse(*context_);
  }
}

llvm::Value *IrGenerator::emitUnionEquality(EmitValue left, types::TypePtr leftType,
                                            EmitValue right, types::TypePtr rightType) {
  llvm::Value *leftTag = nullptr;
  llvm::Value *leftPayload = nullptr;
  if (leftType != nullptr && leftType->kind == TypeKind::Union) {
    llvm::Value *base = asRaw(left);
    leftTag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
    leftPayload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
  } else {
    leftTag = i64(static_cast<std::uint64_t>(unionTagFor(leftType)));
    leftPayload = asRaw(left);
  }
  llvm::Value *rightTag = nullptr;
  llvm::Value *rightPayload = nullptr;
  if (rightType != nullptr && rightType->kind == TypeKind::Union) {
    llvm::Value *base = asRaw(right);
    rightTag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
    rightPayload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
  } else {
    rightTag = i64(static_cast<std::uint64_t>(unionTagFor(rightType)));
    rightPayload = asRaw(right);
  }

  llvm::Type *boolType = builder_->getInt1Ty();
  llvm::BasicBlock *notEqualBlock = createBlock("union.eq.false");
  llvm::BasicBlock *kindBlock = createBlock("union.eq.kind");
  llvm::BasicBlock *numberCheckBlock = createBlock("union.eq.check.number");
  llvm::BasicBlock *numberBlock = createBlock("union.eq.number");
  llvm::BasicBlock *stringCheckBlock = createBlock("union.eq.check.string");
  llvm::BasicBlock *stringBlock = createBlock("union.eq.string");
  llvm::BasicBlock *valueBlock = createBlock("union.eq.value");
  llvm::BasicBlock *mergeBlock = createBlock("union.eq.end");

  builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, rightTag), kindBlock, notEqualBlock);

  builder_->SetInsertPoint(notEqualBlock);
  builder_->CreateBr(mergeBlock);

  builder_->SetInsertPoint(kindBlock);
  builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, i64(unionTagNull)), mergeBlock,
                         numberCheckBlock);

  builder_->SetInsertPoint(numberCheckBlock);
  builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, i64(unionTagNumber)), numberBlock,
                         stringCheckBlock);

  builder_->SetInsertPoint(numberBlock);
  llvm::Value *numberEqual =
      builder_->CreateFCmpOEQ(builder_->CreateBitCast(leftPayload, doubleType_),
                              builder_->CreateBitCast(rightPayload, doubleType_));
  builder_->CreateBr(mergeBlock);

  builder_->SetInsertPoint(stringCheckBlock);
  builder_->CreateCondBr(builder_->CreateICmpEQ(leftTag, i64(unionTagString)), stringBlock,
                         valueBlock);

  builder_->SetInsertPoint(stringBlock);
  llvm::Value *stringEqual = stringEquals(leftPayload, rightPayload);
  builder_->CreateBr(mergeBlock);

  builder_->SetInsertPoint(valueBlock);
  llvm::Value *valueEqual = builder_->CreateICmpEQ(leftPayload, rightPayload);
  builder_->CreateBr(mergeBlock);

  builder_->SetInsertPoint(mergeBlock);
  auto *phi = builder_->CreatePHI(boolType, 5, "union.eq");
  phi->addIncoming(numberEqual, numberBlock);
  phi->addIncoming(stringEqual, stringBlock);
  phi->addIncoming(valueEqual, valueBlock);
  phi->addIncoming(llvm::ConstantInt::getTrue(*context_), kindBlock);
  phi->addIncoming(llvm::ConstantInt::getFalse(*context_), notEqualBlock);
  return phi;
}

IrGenerator::EmitValue IrGenerator::emitZeroValue(types::TypePtr type) {
  const TypePtr resolved = resolve(std::move(type));
  if (resolved == nullptr) return rawValue(i64(0));
  switch (resolved->kind) {
  case TypeKind::Number:
  case TypeKind::Byte:
  case TypeKind::Integer: return numberValue(0.0);
  case TypeKind::String:
  case TypeKind::Bytes: return rawValue(builder_->CreatePtrToInt(emptyString_, i64Type_));
  case TypeKind::Array: {
    llvm::Value *base = allocate(8);
    builder_->CreateStore(i64(0), bytePointer(base, 0));
    return rawValue(base);
  }
  case TypeKind::Struct: {
    const types::TypeLayout layout = types::layoutOf(resolved);
    const std::uint64_t size = layout.defined ? layout.size : 8 * resolved->fields.size();
    llvm::Value *base = allocate(size == 0 ? 1 : size);
    for (std::size_t index = 0; index < resolved->fields.size(); ++index) {
      const TypePtr &fieldType = resolved->fields[index].second;
      const EmitValue field = emitZeroValue(fieldType);
      storeSlot(bytePointer(base, memberOffset(resolved, index)), field, fieldType);
    }
    return rawValue(base);
  }
  case TypeKind::Union: {
    return emitNullishCell(unionTagNull);
  }
  default: return rawValue(i64(0));
  }
}

IrGenerator::EmitValue IrGenerator::emitNullishCell(long long tag) {
  llvm::Value *base = allocate(16);
  builder_->CreateStore(i64(static_cast<std::uint64_t>(tag)), bytePointer(base, 0));
  builder_->CreateStore(i64(0), bytePointer(base, 8));
  return rawValue(base);
}

IrGenerator::EmitValue IrGenerator::emitBox(types::TypePtr payloadType, EmitValue value) {
  llvm::Value *base = allocate(16);
  const long long tag = payloadType != nullptr ? unionTagFor(payloadType) : unionTagPayload;
  builder_->CreateStore(i64(tag), bytePointer(base, 0));
  builder_->CreateStore(asRaw(value), bytePointer(base, 8));
  return rawValue(base);
}

bool IrGenerator::sameLayout(types::TypePtr left, types::TypePtr right) const {
  left = resolve(std::move(left));
  right = resolve(std::move(right));
  if (left == nullptr || right == nullptr || left == right) return true;
  if (left->kind != right->kind || left->kind != TypeKind::Struct) return true;
  if (left->fields.size() != right->fields.size()) return true;
  for (std::size_t index = 0; index < left->fields.size(); ++index) {
    if (left->fields[index].first != right->fields[index].first) return false;
    if (!sameLayout(left->fields[index].second, right->fields[index].second)) return false;
  }
  return true;
}

IrGenerator::EmitValue IrGenerator::convertStruct(types::TypePtr destination, types::TypePtr source,
                                                  EmitValue value) {
  const TypePtr target = resolve(std::move(destination));
  const TypePtr origin = resolve(std::move(source));
  if (target == nullptr || origin == nullptr || target->kind != TypeKind::Struct ||
      origin->kind != TypeKind::Struct)
    return value;
  if (sameLayout(target, origin)) return value;
  const types::TypeLayout targetLayout = types::layoutOf(target);
  const std::uint64_t targetSize =
      targetLayout.defined ? targetLayout.size : 8 * target->fields.size();
  llvm::Value *sourceBase = asRaw(value);
  llvm::Value *targetBase = allocate(targetSize == 0 ? 1 : targetSize);
  for (std::size_t index = 0; index < target->fields.size(); ++index) {
    const std::string &name = target->fields[index].first;
    const TypePtr &targetField = target->fields[index].second;
    std::size_t sourceIndex = origin->fields.size();
    TypePtr sourceField;
    for (std::size_t candidate = 0; candidate < origin->fields.size(); ++candidate)
      if (origin->fields[candidate].first == name) {
        sourceIndex = candidate;
        sourceField = origin->fields[candidate].second;
        break;
      }
    if (sourceIndex >= origin->fields.size()) continue;
    EmitValue field =
        loadSlot(bytePointer(sourceBase, memberOffset(origin, sourceIndex)), sourceField);
    const TypePtr resolvedSource = resolve(sourceField);
    if (resolvedSource != nullptr && resolvedSource->kind == TypeKind::Struct)
      field = convertStruct(targetField, sourceField, field);
    storeSlot(bytePointer(targetBase, memberOffset(target, index)), field, targetField);
  }
  return rawValue(targetBase);
}

IrGenerator::EmitValue IrGenerator::coerce(types::TypePtr destination, types::TypePtr source,
                                           EmitValue value) {
  const TypePtr target = resolve(std::move(destination));
  const TypePtr origin = resolve(std::move(source));
  if (target == nullptr || origin == nullptr) return value;
  if (target->kind == TypeKind::Union) {
    if (origin->kind == TypeKind::Union) return value;
    if (origin->kind == TypeKind::Null) return emitNullishCell(unionTagNull);
    return emitBox(origin, value);
  }
  if (origin->kind == TypeKind::Null) return emitZeroValue(target);
  if (origin->kind == TypeKind::Boolean && target->kind != TypeKind::Boolean) {
    llvm::Value *truthy =
        value.number
            ? builder_->CreateFCmpONE(value.value, llvm::ConstantFP::get(doubleType_, 0.0))
            : builder_->CreateICmpNE(asRaw(value), i64(0));
    return coerce(destination, types_->number(),
                  EmitValue{builder_->CreateSIToFP(builder_->CreateZExt(truthy, i64Type_),
                                                   doubleType_),
                            true});
  }
  if (target->kind == TypeKind::Integer) {
    if (target->length == 0 || target->length > 64) {
      llvm::Value *truncated =
          builder_->CreateIntrinsic(llvm::Intrinsic::trunc, {doubleType_}, {value.value});
      return EmitValue{truncated, true};
    }
    llvm::Value *bits =
        target->isSigned
            ? builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat,
                                        {i64Type_, doubleType_}, {value.value})
            : builder_->CreateIntrinsic(llvm::Intrinsic::fptoui_sat,
                                        {i64Type_, doubleType_}, {value.value});
    if (target->length < 64) {
      llvm::Type *narrow = builder_->getIntNTy(static_cast<unsigned>(target->length));
      bits = builder_->CreateTrunc(bits, narrow);
      bits = target->isSigned ? builder_->CreateSExt(bits, i64Type_)
                              : builder_->CreateZExt(bits, i64Type_);
    }
    return EmitValue{target->isSigned ? builder_->CreateSIToFP(bits, doubleType_)
                                      : builder_->CreateUIToFP(bits, doubleType_),
                      true};
  }
  if (target->kind == TypeKind::Struct && origin->kind == TypeKind::Struct)
    return convertStruct(target, origin, value);
  if (target->kind == TypeKind::Bytes && origin->kind == TypeKind::String) {
    return copyText(value);
  }
  if (target->kind == TypeKind::Bytes && origin->kind == TypeKind::Array) {
    return convertArrayToBytes(value, origin);
  }
  return value;
}

IrGenerator::EmitValue IrGenerator::copyText(EmitValue value) {
  return EmitValue{builder_->CreateCall(bytesCopyFunction(), {asRaw(value)}), false};
}

IrGenerator::EmitValue IrGenerator::convertArrayToBytes(EmitValue value, types::TypePtr arrayType) {
  const TypePtr source = resolve(arrayType);
  const TypePtr elementType =
      source != nullptr && source->kind == TypeKind::Array ? source->element : nullptr;
  const std::uint64_t stride = elementSize(elementType);
  llvm::Value *arrayBase = asRaw(value);
  llvm::Value *length = builder_->CreateLoad(i64Type_, bytePointer(arrayBase, 0));
  llvm::Value *base =
      builder_->CreateCall(allocFunction(), {builder_->CreateAdd(i64(8), length)});
  builder_->CreateStore(length, bytePointer(base, 0));
  llvm::BasicBlock *preheader = builder_->GetInsertBlock();
  llvm::BasicBlock *condition = createBlock("bytes.convert.cond");
  llvm::BasicBlock *body = createBlock("bytes.convert.body");
  llvm::BasicBlock *end = createBlock("bytes.convert.end");
  builder_->CreateBr(condition);
  builder_->SetInsertPoint(condition);
  auto *index = builder_->CreatePHI(i64Type_, 2, "bytes.convert.index");
  index->addIncoming(i64(0), preheader);
  builder_->CreateCondBr(builder_->CreateICmpULT(index, length), body, end);
  builder_->SetInsertPoint(body);
  EmitValue element = loadSlot(
      dynamicPointer(arrayBase,
                     builder_->CreateAdd(i64(8), builder_->CreateMul(index, i64(stride)))),
      elementType);
  llvm::Value *numeric = isNumber(elementType) ? toInt32(element.value) : asRaw(element);
  builder_->CreateStore(builder_->CreateTrunc(numeric, i8Type_),
                        dynamicPointer(base, builder_->CreateAdd(i64(8), index)));
  llvm::Value *next = builder_->CreateAdd(index, i64(1));
  llvm::BasicBlock *bodyEnd = builder_->GetInsertBlock();
  if (!terminated()) builder_->CreateBr(condition);
  index->addIncoming(next, bodyEnd);
  builder_->SetInsertPoint(end);
  return rawValue(base);
}

void IrGenerator::generateDeclaration(const reader::VariableDeclaration &declaration) {
  for (const auto &declarator : declaration.declarations) {
    const auto declared = annotations_->declarationTypes.find(&declarator);
    const TypePtr declaredType =
        declared != annotations_->declarationTypes.end() ? resolve(declared->second) : nullptr;
    llvm::Value *address =
        (generatingEntry_ && scopes_.size() == 1)
            ? static_cast<llvm::Value *>(globalFor(declarator.name, declaredType))
            : static_cast<llvm::Value *>(slotAlloca(allocateSlot(), declaredType));
    scopes_.back().slots.insert_or_assign(declarator.name, Slot{address});

    if (declarator.initializer != nullptr) {
      EmitValue value = generateExpression(declarator.initializer);
      if (declaredType != nullptr)
        value = coerce(declaredType, typeOf(declarator.initializer), value);
      storeSlot(address, value, declaredType);
    } else {
      storeSlot(address, emitZeroValue(declaredType), declaredType);
    }
  }
}

void IrGenerator::generateStatements(const reader::StatementList &statements) {
  for (const auto &statement : statements) {
    if (terminated()) return;
    generateStatement(*statement);
  }
}

void IrGenerator::generateStatement(const reader::Statement &statement) {
  std::visit(
      [&](const auto &node) {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::VariableDeclaration>) {
          generateDeclaration(node);
        } else if constexpr (std::is_same_v<T, reader::ExpressionStatement>) {
          if (node.expression != nullptr) (void)generateExpression(node.expression);
        } else if constexpr (std::is_same_v<T, reader::FunctionDeclaration>) {
        } else if constexpr (std::is_same_v<T, reader::ReturnStatement>) {
          EmitValue value = rawValue(i64(0));
          if (node.argument != nullptr) {
            value = generateExpression(node.argument);
            value = coerce(returnType_, typeOf(node.argument), value);
          } else {
            value = coerce(returnType_, types_->nullType(), value);
          }
          builder_->CreateStore(asRaw(value), returnSlot_);
          builder_->CreateBr(returnBlock_);
        } else if constexpr (std::is_same_v<T, reader::BlockStatement>) {
          beginScope();
          generateStatements(node.statements);
          endScope();
        } else if constexpr (std::is_same_v<T, reader::IfStatement>) {
          EmitValue condition = generateExpression(node.condition);
          llvm::Value *test = emitTruthy(condition, typeOf(node.condition));
          llvm::BasicBlock *thenBlock = createBlock("if.then");
          llvm::BasicBlock *elseBlock = !node.alternate.empty() ? createBlock("if.else") : nullptr;
          llvm::BasicBlock *endBlock = createBlock("if.end");
          builder_->CreateCondBr(test, thenBlock, elseBlock != nullptr ? elseBlock : endBlock);
          builder_->SetInsertPoint(thenBlock);
          beginScope();
          generateStatements(node.consequent);
          endScope();
          if (!terminated()) builder_->CreateBr(endBlock);
          if (elseBlock != nullptr) {
            builder_->SetInsertPoint(elseBlock);
            beginScope();
            generateStatements(node.alternate);
            endScope();
            if (!terminated()) builder_->CreateBr(endBlock);
          }
          builder_->SetInsertPoint(endBlock);
        } else if constexpr (std::is_same_v<T, reader::WhileStatement>) {
          llvm::BasicBlock *conditionBlock = createBlock("while.cond");
          llvm::BasicBlock *bodyBlock = createBlock("while.body");
          llvm::BasicBlock *endBlock = createBlock("while.end");
          builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(conditionBlock);
          EmitValue condition = generateExpression(node.condition);
          builder_->CreateCondBr(emitTruthy(condition, typeOf(node.condition)), bodyBlock,
                                 endBlock);
          builder_->SetInsertPoint(bodyBlock);
          loops_.push_back(Loop{endBlock, conditionBlock});
          const bool labelled = !pendingLoopLabel_.empty();
          if (labelled) {
            labels_.push_back(Label{pendingLoopLabel_, endBlock, conditionBlock});
            pendingLoopLabel_.clear();
          }
          beginScope();
          generateStatements(node.body);
          endScope();
          loops_.pop_back();
          if (labelled) labels_.pop_back();
          if (!terminated()) builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(endBlock);
        } else if constexpr (std::is_same_v<T, reader::SwitchStatement>) {
          EmitValue discriminant = generateExpression(node.discriminant);
          const types::TypePtr discriminantType = typeOf(node.discriminant);
          const bool textDiscriminant = isText(discriminantType);
          llvm::BasicBlock *endBlock = createBlock("switch.end");
          std::vector<llvm::BasicBlock *> clauseBlocks;
          clauseBlocks.reserve(node.cases.size());
          for (std::size_t clause = 0; clause < node.cases.size(); ++clause)
            clauseBlocks.push_back(createBlock("switch.case"));
          llvm::BasicBlock *defaultBlock = endBlock;
          for (std::size_t clause = 0; clause < node.cases.size(); ++clause)
            if (node.cases[clause].value == nullptr) defaultBlock = clauseBlocks[clause];
          llvm::BasicBlock *testBlock = builder_->GetInsertBlock();
          for (std::size_t clause = 0; clause < node.cases.size(); ++clause) {
            if (node.cases[clause].value == nullptr) continue;
            builder_->SetInsertPoint(testBlock);
            const types::TypePtr caseType = typeOf(node.cases[clause].value);
            const bool caseText = isText(caseType);
            if (caseText != textDiscriminant)
              throw errors::ErrorException{
                  errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "case",
                                      node.cases[clause].range}};
            llvm::Value *match = nullptr;
            if (caseText) {
              match = stringEquals(asRaw(discriminant),
                                   asRaw(generateExpression(node.cases[clause].value)));
            } else {
              const auto asDouble = [&](EmitValue value) -> llvm::Value * {
                if (value.number) return value.value;
                return builder_->CreateSIToFP(asRaw(value), doubleType_);
              };
              EmitValue caseValue = generateExpression(node.cases[clause].value);
              match = builder_->CreateFCmpOEQ(asDouble(discriminant), asDouble(caseValue));
            }
            llvm::BasicBlock *nextTest = createBlock("switch.test");
            builder_->CreateCondBr(match, clauseBlocks[clause], nextTest);
            testBlock = nextTest;
          }
          builder_->SetInsertPoint(testBlock);
          builder_->CreateBr(defaultBlock);
          loops_.push_back(
              Loop{endBlock, loops_.empty() ? nullptr : loops_.back().continueBlock});
          beginScope();
          for (std::size_t clause = 0; clause < node.cases.size(); ++clause) {
            builder_->SetInsertPoint(clauseBlocks[clause]);
            generateStatements(node.cases[clause].statements);
            if (!terminated())
              builder_->CreateBr(clause + 1 < node.cases.size() ? clauseBlocks[clause + 1]
                                                               : endBlock);
          }
          endScope();
          loops_.pop_back();
          builder_->SetInsertPoint(endBlock);
        } else if constexpr (std::is_same_v<T, reader::ForStatement>) {
          llvm::BasicBlock *conditionBlock = createBlock("for.cond");
          llvm::BasicBlock *bodyBlock = createBlock("for.body");
          llvm::BasicBlock *updateBlock = createBlock("for.update");
          llvm::BasicBlock *endBlock = createBlock("for.end");
          beginScope();
          if (node.init != nullptr) generateDeclaration(*node.init);
          if (node.initExpression != nullptr) (void)generateExpression(node.initExpression);
          builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(conditionBlock);
          if (node.condition != nullptr) {
            EmitValue condition = generateExpression(node.condition);
            builder_->CreateCondBr(emitTruthy(condition, typeOf(node.condition)), bodyBlock,
                                   endBlock);
          } else {
            builder_->CreateBr(bodyBlock);
          }
          builder_->SetInsertPoint(bodyBlock);
          loops_.push_back(Loop{endBlock, updateBlock});
          const bool labelled = !pendingLoopLabel_.empty();
          if (labelled) {
            labels_.push_back(Label{pendingLoopLabel_, endBlock, updateBlock});
            pendingLoopLabel_.clear();
          }
          beginScope();
          generateStatements(node.body);
          endScope();
          loops_.pop_back();
          if (labelled) labels_.pop_back();
          if (!terminated()) builder_->CreateBr(updateBlock);
          builder_->SetInsertPoint(updateBlock);
          if (node.update != nullptr) (void)generateExpression(node.update);
          if (!terminated()) builder_->CreateBr(conditionBlock);
          builder_->SetInsertPoint(endBlock);
          endScope();
        } else if constexpr (std::is_same_v<T, reader::BreakStatement>) {
          if (node.label != nullptr) {
            llvm::BasicBlock *target = nullptr;
            for (auto entry = labels_.rbegin(); entry != labels_.rend(); ++entry)
              if (entry->name == node.label->text) {
                target = entry->breakBlock;
                break;
              }
            if (target == nullptr)
              throw errors::ErrorException{errors::ErrorStatus{
                  errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER, node.label->text, statement.range}};
            builder_->CreateBr(target);
          } else {
            builder_->CreateBr(loops_.back().breakBlock);
          }
        } else if constexpr (std::is_same_v<T, reader::ContinueStatement>) {
          if (node.label != nullptr) {
            llvm::BasicBlock *target = nullptr;
            for (auto entry = labels_.rbegin(); entry != labels_.rend(); ++entry)
              if (entry->name == node.label->text && entry->continueBlock != nullptr) {
                target = entry->continueBlock;
                break;
              }
            if (target == nullptr)
              throw errors::ErrorException{
                  errors::ErrorStatus{errors::ErrorCode::TYPE_CONTINUE_OUTSIDE_LOOP,
                                      node.label->text, statement.range}};
            builder_->CreateBr(target);
          } else {
            builder_->CreateBr(loops_.back().continueBlock);
          }
        } else if constexpr (std::is_same_v<T, reader::ThrowStatement>) {
          if (node.argument != nullptr) (void)generateExpression(node.argument);
          builder_->CreateCall(throwFunction());
          builder_->CreateUnreachable();
        } else if constexpr (std::is_same_v<T, reader::LabeledStatement>) {
          if (node.body == nullptr) return;
          const bool loopBody = std::holds_alternative<reader::WhileStatement>(node.body->node) ||
                                std::holds_alternative<reader::ForStatement>(node.body->node);
          if (loopBody) {
            pendingLoopLabel_ = node.label;
            generateStatement(*node.body);
            pendingLoopLabel_.clear();
          } else {
            llvm::BasicBlock *endBlock = createBlock("label.end");
            labels_.push_back(Label{node.label, endBlock, nullptr});
            generateStatement(*node.body);
            labels_.pop_back();
            if (!terminated()) builder_->CreateBr(endBlock);
            builder_->SetInsertPoint(endBlock);
          }
        }
      },
      statement.node);
}

IrGenerator::EmitValue IrGenerator::generateExpression(const reader::ExprPtr &expression) {
  if (terminated()) {
    const TypePtr type = resolve(typeOf(expression));
    if (type != nullptr && type->kind == TypeKind::Number) return numberValue(0.0);
    return rawValue(i64(0));
  }
  return std::visit(
      [&](const auto &node) -> EmitValue {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, reader::NumberLiteralExpr>) {
          return numberValue(parseNumber(node.spelling));
        } else if constexpr (std::is_same_v<T, reader::StringLiteralExpr>) {
          return rawValue(builder_->CreatePtrToInt(internString(node.value), i64Type_));
        } else if constexpr (std::is_same_v<T, reader::BytesLiteralExpr>) {
          return copyText(
              rawValue(builder_->CreatePtrToInt(internString(node.value), i64Type_)));
        } else if constexpr (std::is_same_v<T, reader::BooleanLiteralExpr>) {
          return rawValue(i64(node.value ? 1 : 0));
        } else if constexpr (std::is_same_v<T, reader::NullLiteralExpr>) {
          return rawValue(i64(0));
        } else if constexpr (std::is_same_v<T, reader::UndefinedLiteralExpr>) {
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "undefined", expression->range}};
        } else if constexpr (std::is_same_v<T, reader::IdentifierExpr>) {
          if (Slot *slot = lookupSlot(node.name))
            return loadSlot(slot->address, typeOf(expression));
          const auto global = globals_.find(node.name);
          if (global != globals_.end()) return loadSlot(global->second, typeOf(expression));
          const auto function = functions_.find(node.name);
          if (function != functions_.end())
            return rawValue(builder_->CreatePtrToInt(function->second, i64Type_));
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_UNDEFINED_IDENTIFIER, node.name, expression->range}};
        } else if constexpr (std::is_same_v<T, reader::UnaryExpr>) {
          return generateUnary(node);
        } else if constexpr (std::is_same_v<T, reader::AddressOfExpr>) {
          // For &member_expr, return the address directly without loading
          if (const auto *member = std::get_if<reader::MemberExpr>(&node.operand->node)) {
            llvm::Value *addr = generateMemberAddress(*member, node.operand->range);
            return rawValue(builder_->CreatePtrToInt(addr, i64Type_));
          }
          // For &*deref_expr, return the pointer value directly
          if (const auto *deref = std::get_if<reader::DereferenceExpr>(&node.operand->node)) {
            EmitValue ptrValue = generateExpression(deref->operand);
            return rawValue(asRaw(ptrValue));
          }
          if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&node.operand->node)) {
            if (Slot *slot = lookupSlot(identifier->name)) {
              return rawValue(builder_->CreatePtrToInt(slot->address, i64Type_));
            }
            const auto global = globals_.find(identifier->name);
            if (global != globals_.end())
              return rawValue(builder_->CreatePtrToInt(global->second, i64Type_));
          }
          EmitValue operand = generateExpression(node.operand);
          return rawValue(asRaw(operand));
        } else if constexpr (std::is_same_v<T, reader::DereferenceExpr>) {
          EmitValue ptrValue = generateExpression(node.operand);
          llvm::Value *pointer = builder_->CreateIntToPtr(asRaw(ptrValue), ptrType_);
          const TypePtr pointedType = resolve(typeOf(expression));
          return loadSlot(pointer, pointedType);
        } else if constexpr (std::is_same_v<T, reader::BinaryExpr>) {
          return generateBinary(node);
        } else if constexpr (std::is_same_v<T, reader::AssignmentExpr>) {
          return generateAssignment(node);
        } else if constexpr (std::is_same_v<T, reader::MemberExpr>) {
          llvm::Value *address = generateMemberAddress(node, expression->range);
          if (node.computedFlag) {
            const TypePtr objectType = resolve(typeOf(node.object));
            if (objectType != nullptr && objectType->kind == TypeKind::Bytes)
              return loadByteElement(address);
          }
          return loadSlot(builder_->CreateIntToPtr(address, ptrType_), typeOf(expression));
        } else if constexpr (std::is_same_v<T, reader::ObjectExpr>) {
          const TypePtr structType = resolve(typeOf(expression));
          const bool structured = structType != nullptr && structType->kind == TypeKind::Struct;
          const types::TypeLayout layout =
              structured ? types::layoutOf(structType) : types::TypeLayout{};
          const std::uint64_t structSize =
              structured && layout.defined ? layout.size : 8 * node.properties.size();
          llvm::Value *base = allocate(structSize == 0 ? 1 : structSize);
          for (std::size_t index = 0; index < node.properties.size(); ++index) {
            std::size_t fieldIndex = index;
            TypePtr declaredField;
            if (structured) {
              for (std::size_t field = 0; field < structType->fields.size(); ++field)
                if (structType->fields[field].first == node.properties[index].first) {
                  fieldIndex = field;
                  declaredField = structType->fields[field].second;
                  break;
                }
            }
            EmitValue value = generateExpression(node.properties[index].second);
            if (declaredField != nullptr)
              value = coerce(declaredField, typeOf(node.properties[index].second), value);
            const std::uint64_t offset =
                structured ? memberOffset(structType, fieldIndex) : 8 * fieldIndex;
            storeSlot(bytePointer(base, offset), value, declaredField);
          }
          return rawValue(base);
        } else if constexpr (std::is_same_v<T, reader::ArrayExpr>) {
          // Determine array size: max of element count and highest designated index + 1
          std::size_t arraySize = node.elements.size();
          for (const auto &desig : node.designated)
            if (desig.index + 1 > arraySize) arraySize = desig.index + 1;
          const TypePtr arrayType = resolve(typeOf(expression));
          TypePtr elementType = nullptr;
          if (arrayType != nullptr && arrayType->kind == TypeKind::Array) {
            elementType = arrayType->element;
            if (arrayType->length > arraySize) arraySize = arrayType->length;
          }

          const std::uint64_t stride = elementSize(elementType);
          llvm::Value *base = allocate(8 + stride * arraySize);
          builder_->CreateStore(i64(arraySize), bytePointer(base, 0));

          // Zero-initialize all slots
          for (std::size_t index = 0; index < arraySize; ++index) {
            EmitValue zero = emitZeroValue(elementType != nullptr ? elementType : types_->nullType());
            storeSlot(bytePointer(base, 8 + stride * index), zero, elementType);
          }

          // Store direct elements
          for (std::size_t index = 0; index < node.elements.size(); ++index) {
            EmitValue value = generateExpression(node.elements[index]);
            if (elementType != nullptr)
              value = coerce(elementType, typeOf(node.elements[index]), value);
            storeSlot(bytePointer(base, 8 + stride * index), value, elementType);
          }

          // Store designated initializers
          for (const auto &desig : node.designated) {
            EmitValue value = generateExpression(desig.value);
            if (elementType != nullptr)
              value = coerce(elementType, typeOf(desig.value), value);
            storeSlot(bytePointer(base, 8 + stride * desig.index), value, elementType);
          }
          return rawValue(base);
        } else if constexpr (std::is_same_v<T, reader::CallExpr>) {
          return generateCall(expression, node);
        } else if constexpr (std::is_same_v<T, reader::TypeOperandExpr>) {
          throw errors::ErrorException{errors::ErrorStatus{
              errors::ErrorCode::TYPE_TYPE_USED_AS_VALUE, {}, expression->range}};
        }
        return rawValue(i64(0));
      },
      expression->node);
}

IrGenerator::EmitValue IrGenerator::generateUnary(const reader::UnaryExpr &unary) {
  if (unary.op == "!") {
    EmitValue operand = generateExpression(unary.operand);
    llvm::Value *result = builder_->CreateNot(emitTruthy(operand, typeOf(unary.operand)));
    return rawValue(builder_->CreateZExt(result, i64Type_));
  }
  if (unary.op == "~") {
    EmitValue operand = generateExpression(unary.operand);
    llvm::Value *value = toInt32(operand.value);
    llvm::Value *inverted = builder_->CreateNot(value);
    llvm::Value *extended = builder_->CreateSExt(inverted, i64Type_);
    return EmitValue{builder_->CreateSIToFP(extended, doubleType_), true};
  }
  if (unary.op == "-") {
    EmitValue operand = generateExpression(unary.operand);
    return EmitValue{builder_->CreateFNeg(operand.value), true};
  }
  if (unary.op == "+") {
    EmitValue operand = generateExpression(unary.operand);
    return EmitValue{builder_->CreateBitCast(asRaw(operand), doubleType_), true};
  }
  if (unary.op == "typeof") {
    const TypePtr operandType = resolve(typeOf(unary.operand));
    EmitValue operand = generateExpression(unary.operand);
    if (operandType != nullptr && operandType->kind == TypeKind::Union) {
      llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(asRaw(operand), 0));
      llvm::Value *nullText = builder_->CreatePtrToInt(internString("null"), i64Type_);
      llvm::Value *numberText = builder_->CreatePtrToInt(internString("f64"), i64Type_);
      llvm::Value *stringText = builder_->CreatePtrToInt(internString("string"), i64Type_);
      llvm::Value *boolText = builder_->CreatePtrToInt(internString("boolean"), i64Type_);
      llvm::Value *payloadText =
          builder_->CreatePtrToInt(internString(typeofName(unionPayload(operandType))), i64Type_);

      // Start with payload as default
      llvm::Value *result = payloadText;
      result = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagBoolean)), boolText, result);
      result = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagString)), stringText, result);
      result = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagNumber)), numberText, result);
      result = builder_->CreateSelect(
          builder_->CreateICmpEQ(tag, i64(unionTagNull)), nullText, result);
      return rawValue(result);
    }
    return rawValue(builder_->CreatePtrToInt(internString(typeofName(operandType)), i64Type_));
  }
  if (unary.op == "void") {
    (void)generateExpression(unary.operand);
    return rawValue(i64(0));
  }
  throw errors::ErrorException{
      errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, unary.op, {}}};
}

IrGenerator::EmitValue IrGenerator::generateBinary(const reader::BinaryExpr &binary) {
  if (binary.op == "==" || binary.op == "!=" || binary.op == "===" || binary.op == "!==")
    return generateEquality(binary);
  if (binary.op == "&&" || binary.op == "||") return generateLogical(binary);
  if (binary.op == "+" || binary.op == "-" || binary.op == "*" || binary.op == "/" ||
      binary.op == "%" || binary.op == "**")
    return generateArithmetic(binary);
  if (binary.op == "<" || binary.op == ">" || binary.op == "<=" || binary.op == ">=")
    return generateRelational(binary);
  return generateBitwise(binary);
}

IrGenerator::EmitValue IrGenerator::generateArithmetic(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  EmitValue right = generateExpression(binary.right);
  const TypePtr leftType = resolve(typeOf(binary.left));
  if (leftType != nullptr && leftType->kind == TypeKind::Pointer) {
    const TypePtr rightType = resolve(typeOf(binary.right));
    if (rightType != nullptr && rightType->kind == TypeKind::Pointer) {
      llvm::Value *difference = builder_->CreateSub(asRaw(left), asRaw(right));
      return EmitValue{builder_->CreateSIToFP(
                           builder_->CreateSDiv(difference, i64(elementSize(leftType->pointed))),
                           doubleType_),
                       true};
    }
    llvm::Value *offset = builder_->CreateMul(toInt64(right), i64(elementSize(leftType->pointed)));
    if (binary.op == "+") return rawValue(builder_->CreateAdd(asRaw(left), offset));
    if (binary.op == "-") return rawValue(builder_->CreateSub(asRaw(left), offset));
  }
  if (binary.op == "+") return EmitValue{builder_->CreateFAdd(left.value, right.value), true};
  if (binary.op == "-") return EmitValue{builder_->CreateFSub(left.value, right.value), true};
  if (binary.op == "*") return EmitValue{builder_->CreateFMul(left.value, right.value), true};
  if (binary.op == "/") return EmitValue{builder_->CreateFDiv(left.value, right.value), true};
  if (binary.op == "%")
    return EmitValue{builder_->CreateCall(moduloFunction(), {left.value, right.value}), true};
  return EmitValue{builder_->CreateCall(powerFunction(), {left.value, right.value}), true};
}

IrGenerator::EmitValue IrGenerator::generateBitwise(const reader::BinaryExpr &binary) {
  return emitBitwiseOperation(binary.op, generateExpression(binary.left),
                              generateExpression(binary.right));
}

IrGenerator::EmitValue IrGenerator::emitBitwiseOperation(const std::string &op, EmitValue left,
                                                        EmitValue right) {
  llvm::Value *leftValue = toInt32(left.value);
  llvm::Value *rightValue = toInt32(right.value);
  const bool shift = op == "<<" || op == ">>" || op == ">>>";
  if (shift) rightValue = builder_->CreateAnd(rightValue, i32(31));
  if (op == "&")
    return EmitValue{builder_->CreateSIToFP(
                         builder_->CreateSExt(builder_->CreateAnd(leftValue, rightValue), i64Type_),
                         doubleType_),
                     true};
  if (op == "|")
    return EmitValue{builder_->CreateSIToFP(
                         builder_->CreateSExt(builder_->CreateOr(leftValue, rightValue), i64Type_),
                         doubleType_),
                     true};
  if (op == "^")
    return EmitValue{builder_->CreateSIToFP(
                         builder_->CreateSExt(builder_->CreateXor(leftValue, rightValue), i64Type_),
                         doubleType_),
                     true};
  if (op == "<<")
    return EmitValue{builder_->CreateSIToFP(
                         builder_->CreateSExt(builder_->CreateShl(leftValue, rightValue), i64Type_),
                         doubleType_),
                     true};
  if (op == ">>")
    return EmitValue{builder_->CreateSIToFP(
                         builder_->CreateSExt(builder_->CreateAShr(leftValue, rightValue), i64Type_),
                         doubleType_),
                     true};
  return EmitValue{builder_->CreateUIToFP(
                       builder_->CreateZExt(builder_->CreateLShr(leftValue, rightValue), i64Type_),
                       doubleType_),
                   true};
}

IrGenerator::EmitValue IrGenerator::generateRelational(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  EmitValue right = generateExpression(binary.right);
  llvm::Value *comparison = nullptr;
  if (isText(typeOf(binary.left)) && isText(typeOf(binary.right))) {
    llvm::Value *ordering =
        builder_->CreateCall(stringCompareFunction(), {asRaw(left), asRaw(right)});
    if (binary.op == "<") comparison = builder_->CreateICmpSLT(ordering, i64(0));
    else if (binary.op == ">")
      comparison = builder_->CreateICmpSGT(ordering, i64(0));
    else if (binary.op == "<=")
      comparison = builder_->CreateICmpSLE(ordering, i64(0));
    else
      comparison = builder_->CreateICmpSGE(ordering, i64(0));
  } else if (isNumber(typeOf(binary.left)) && isNumber(typeOf(binary.right))) {
    if (binary.op == "<") comparison = builder_->CreateFCmpOLT(left.value, right.value);
    else if (binary.op == ">")
      comparison = builder_->CreateFCmpOGT(left.value, right.value);
    else if (binary.op == "<=")
      comparison = builder_->CreateFCmpOLE(left.value, right.value);
    else
      comparison = builder_->CreateFCmpOGE(left.value, right.value);
  } else {
    llvm::Value *lhs = asRaw(left);
    llvm::Value *rhs = asRaw(right);
    if (binary.op == "<") comparison = builder_->CreateICmpULT(lhs, rhs);
    else if (binary.op == ">")
      comparison = builder_->CreateICmpUGT(lhs, rhs);
    else if (binary.op == "<=")
      comparison = builder_->CreateICmpULE(lhs, rhs);
    else
      comparison = builder_->CreateICmpUGE(lhs, rhs);
  }
  return rawValue(builder_->CreateZExt(comparison, i64Type_));
}

IrGenerator::EmitValue IrGenerator::generateEquality(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  EmitValue right = generateExpression(binary.right);
  const TypePtr leftType = resolve(typeOf(binary.left));
  const TypePtr rightType = resolve(typeOf(binary.right));
  llvm::Value *equal = nullptr;
  const bool leftUnion = leftType != nullptr && leftType->kind == TypeKind::Union;
  const bool rightUnion = rightType != nullptr && rightType->kind == TypeKind::Union;
  if (leftUnion || rightUnion) {
    equal = emitUnionEquality(left, leftType, right, rightType);
  } else if (isNumber(leftType) && isNumber(rightType)) {
    equal = builder_->CreateFCmpOEQ(left.value, right.value);
  } else if (isText(leftType) && isText(rightType)) {
    equal = stringEquals(asRaw(left), asRaw(right));
  } else {
    equal = builder_->CreateICmpEQ(asRaw(left), asRaw(right));
  }
  if (binary.op == "!=" || binary.op == "!==") equal = builder_->CreateNot(equal);
  return rawValue(builder_->CreateZExt(equal, i64Type_));
}

IrGenerator::EmitValue IrGenerator::generateLogical(const reader::BinaryExpr &binary) {
  EmitValue left = generateExpression(binary.left);
  const TypePtr leftType = typeOf(binary.left);
  llvm::Value *leftBits = asRaw(left);
  llvm::BasicBlock *leftBlock = builder_->GetInsertBlock();
  llvm::BasicBlock *rightBlock = createBlock("logic.rhs");
  llvm::BasicBlock *endBlock = createBlock("logic.end");
  const bool andOp = binary.op == "&&";
  llvm::Value *isTruthy = emitTruthy(left, leftType);
  llvm::Value *takeLeft = andOp ? builder_->CreateNot(isTruthy) : isTruthy;
  builder_->CreateCondBr(takeLeft, endBlock, rightBlock);
  builder_->SetInsertPoint(rightBlock);
  EmitValue right = generateExpression(binary.right);
  right = coerce(leftType, typeOf(binary.right), right);
  llvm::Value *rightBits = asRaw(right);
  llvm::BasicBlock *rightEnd = builder_->GetInsertBlock();
  if (!terminated()) builder_->CreateBr(endBlock);
  builder_->SetInsertPoint(endBlock);
  auto *result = builder_->CreatePHI(i64Type_, 2, "logic.value");
  result->addIncoming(leftBits, leftBlock);
  result->addIncoming(rightBits, rightEnd);
  return asTyped(result, leftType);
}

bool IrGenerator::byteElement(const reader::ExprPtr &expression) const {
  const auto *member = std::get_if<reader::MemberExpr>(&expression->node);
  if (member == nullptr || !member->computedFlag) return false;
  const TypePtr objectType = resolve(typeOf(member->object));
  if (objectType == nullptr) return false;
  if (objectType->kind == TypeKind::Bytes) return true;
  return objectType->kind == TypeKind::Pointer && isBytes(objectType->pointed);
}

IrGenerator::EmitValue IrGenerator::loadByteElement(llvm::Value *address) const {
  llvm::Value *value =
      builder_->CreateLoad(i8Type_, builder_->CreateIntToPtr(address, ptrType_));
  return EmitValue{builder_->CreateSIToFP(builder_->CreateSExt(value, i64Type_), doubleType_),
                   true};
}

void IrGenerator::storeByteElement(llvm::Value *address, EmitValue value) const {
  builder_->CreateStore(builder_->CreateTrunc(toInt32(value.value), i8Type_),
                        builder_->CreateIntToPtr(address, ptrType_));
}

llvm::Value *IrGenerator::generateLValueAddress(const reader::ExprPtr &target) {
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&target->node)) {
    if (Slot *slot = lookupSlot(identifier->name)) return slot->address;
    const auto global = globals_.find(identifier->name);
    if (global != globals_.end()) return global->second;
    return nullptr;
  }
  if (const auto *deref = std::get_if<reader::DereferenceExpr>(&target->node))
    return builder_->CreateIntToPtr(asRaw(generateExpression(deref->operand)), ptrType_);
  if (const auto *member = std::get_if<reader::MemberExpr>(&target->node))
    return builder_->CreateIntToPtr(generateMemberAddress(*member, target->range), ptrType_);
  return nullptr;
}

IrGenerator::EmitValue IrGenerator::generateAssignment(const reader::AssignmentExpr &assignment) {
  EmitValue value = generateExpression(assignment.right);
  TypePtr valueType = typeOf(assignment.right);
  TypePtr targetType = typeOf(assignment.left);

  llvm::Value *address = generateLValueAddress(assignment.left);
  if (address == nullptr)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_INVALID_ASSIGNMENT_TARGET, assignment.op, assignment.left->range}};

  const TypePtr resolvedTarget = resolve(targetType);
  if (assignment.op != "=" && resolvedTarget != nullptr &&
      resolvedTarget->kind == TypeKind::Pointer) {
    const std::string pointerOp = assignment.op.substr(0, assignment.op.size() - 1);
    if (pointerOp != "+" && pointerOp != "-")
      throw errors::ErrorException{errors::ErrorStatus{
          errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, assignment.op, assignment.left->range}};
    EmitValue current = loadSlot(address, targetType);
    llvm::Value *offset =
        builder_->CreateMul(toInt64(value), i64(elementSize(resolvedTarget->pointed)));
    EmitValue result = rawValue(pointerOp == "+"
                                    ? builder_->CreateAdd(asRaw(current), offset)
                                    : builder_->CreateSub(asRaw(current), offset));
    storeSlot(address, result, targetType);
    return result;
  }

  const bool byteTarget = byteElement(assignment.left);
  if (assignment.op != "=") {
    EmitValue current = byteTarget ? loadByteElement(address) : loadSlot(address, targetType);
    const std::string op = assignment.op.substr(0, assignment.op.size() - 1);
    if (op == "+")
      value = EmitValue{builder_->CreateFAdd(current.value, value.value), true};
    else if (op == "-")
      value = EmitValue{builder_->CreateFSub(current.value, value.value), true};
    else if (op == "*")
      value = EmitValue{builder_->CreateFMul(current.value, value.value), true};
    else if (op == "/")
      value = EmitValue{builder_->CreateFDiv(current.value, value.value), true};
    else if (op == "%")
      value = EmitValue{builder_->CreateCall(moduloFunction(), {current.value, value.value}), true};
    else if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>" || op == ">>>")
      value = emitBitwiseOperation(op, current, value);
    else
      throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR,
                                                       assignment.op, assignment.left->range}};
    valueType = types_->number();
  }

  value = coerce(targetType, valueType, value);
  if (byteTarget)
    storeByteElement(address, value);
  else
    storeSlot(address, value, targetType);
  return value;
}

llvm::Value *IrGenerator::generateMemberAddress(const reader::MemberExpr &member,
                                                const reader::SourceRange &range) {
  EmitValue object = generateExpression(member.object);
  llvm::Value *base = asRaw(object);
  TypePtr objectType = resolve(typeOf(member.object));

  if (member.computedFlag && objectType != nullptr && objectType->kind == TypeKind::Pointer) {
    const TypePtr pointed = resolve(objectType->pointed);
    if (pointed != nullptr && pointed->kind != TypeKind::Array &&
        pointed->kind != TypeKind::Bytes) {
      EmitValue indexValue = generateExpression(member.computed);
      llvm::Value *offset = builder_->CreateMul(toInt64(indexValue), i64(elementSize(pointed)));
      return builder_->CreateAdd(base, offset);
    }
  }

  // Dereference pointer if needed - for member access, we keep the pointer
  // value as the base and add the field offset directly
  if (objectType != nullptr && objectType->kind == TypeKind::Pointer) {
    objectType = objectType->pointed;
  }

  if (member.computedFlag) {
    // Check if this is a range expression (slicing)
    if (const auto *rangeExpr =
            std::get_if<reader::RangeExpr>(&member.computed->node)) {
      // String or bytes slicing: obj[start:end]
      if (objectType == nullptr ||
          (objectType->kind != TypeKind::String && objectType->kind != TypeKind::Bytes))
        throw errors::ErrorException{
            errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "[]", range}};

      EmitValue startValue = generateExpression(rangeExpr->start);
      EmitValue endValue = generateExpression(rangeExpr->end);
      llvm::Value *startIdx = toInt64(startValue);
      llvm::Value *endIdx = toInt64(endValue);

      llvm::Value *lengthPtr = bytePointer(base, 0);
      llvm::Value *length = builder_->CreateLoad(i64Type_, lengthPtr);

      llvm::BasicBlock *oobBlock = createBlock("slice.oob");
      llvm::BasicBlock *okBlock = createBlock("slice.ok");
      llvm::BasicBlock *endBlock = createBlock("slice.end");

      // Check start < length and end <= length and start <= end
      llvm::Value *startOk = builder_->CreateICmpULT(startIdx, length);
      llvm::Value *endOk = builder_->CreateICmpULE(endIdx, length);
      llvm::Value *startLeEnd = builder_->CreateICmpSLE(startIdx, endIdx);
      llvm::Value *boundsOk =
          builder_->CreateAnd(builder_->CreateAnd(startOk, endOk), startLeEnd);

      builder_->CreateCondBr(boundsOk, okBlock, oobBlock);

      builder_->SetInsertPoint(oobBlock);
      builder_->CreateCall(boundsFunction());
      builder_->CreateBr(endBlock);

      builder_->SetInsertPoint(okBlock);
      // Compute slice pointer: base + 8 + start
      llvm::Value *slicePtr =
          builder_->CreateAdd(base, builder_->CreateAdd(i64(8), startIdx));
      // Compute slice length: end - start
      llvm::Value *sliceLen = builder_->CreateSub(endIdx, startIdx);

      // Allocate 16 bytes for the result string descriptor {ptr, len}
      llvm::Value *resultDesc = allocate(16);
      builder_->CreateStore(slicePtr, bytePointer(resultDesc, 0));
      builder_->CreateStore(sliceLen, bytePointer(resultDesc, 8));

      builder_->CreateBr(endBlock);

      builder_->SetInsertPoint(endBlock);
      llvm::PHINode *ptrPhi = builder_->CreatePHI(i64Type_, 2, "slice.ptr");
      ptrPhi->addIncoming(resultDesc, okBlock);
      ptrPhi->addIncoming(builder_->CreatePtrToInt(builder_->CreateIntToPtr(i64(0), ptrType_), i64Type_),
                          oobBlock);
      return ptrPhi;
    }

    // Pointer arithmetic: ptr + index * sizeof(element)
    if (objectType != nullptr && objectType->kind == TypeKind::Pointer) {
      EmitValue indexValue = generateExpression(member.computed);
      llvm::Value *index = toInt64(indexValue);
      llvm::Value *stride = i64(elementSize(objectType->pointed));
      llvm::Value *offset = builder_->CreateMul(index, stride);
      return builder_->CreateAdd(base, offset);
    }

    // Indexing: obj[index]
    if (objectType == nullptr ||
        (objectType->kind != TypeKind::Array && objectType->kind != TypeKind::Bytes &&
         objectType->kind != TypeKind::String))
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::TYPE_UNSUPPORTED_OPERATOR, "[]", range}};
    EmitValue indexValue = generateExpression(member.computed);
    llvm::Value *index32 = toInt32(indexValue.value);
    llvm::Value *index = builder_->CreateSExt(index32, i64Type_);

    llvm::Value *lengthPtr = bytePointer(base, 0);
    llvm::Value *length = builder_->CreateLoad(i64Type_, lengthPtr);

    llvm::BasicBlock *oobBlock = createBlock("oob");
    llvm::BasicBlock *okBlock = createBlock("member.ok");
    llvm::BasicBlock *endBlock = createBlock("member.end");
    builder_->CreateCondBr(builder_->CreateICmpUGE(index, length), oobBlock, okBlock);

    builder_->SetInsertPoint(oobBlock);
    builder_->CreateCall(boundsFunction());
    builder_->CreateBr(endBlock);

    builder_->SetInsertPoint(okBlock);
    const std::uint64_t stride =
        objectType->kind == TypeKind::Bytes ? 1 : elementSize(objectType->element);
    llvm::Value *elementPtr =
        dynamicPointer(base, builder_->CreateAdd(i64(8), builder_->CreateMul(index, i64(stride))));
    llvm::Value *okAddress = builder_->CreatePtrToInt(elementPtr, i64Type_);
    builder_->CreateBr(endBlock);

    builder_->SetInsertPoint(endBlock);
    llvm::PHINode *phi = builder_->CreatePHI(i64Type_, 2, "member.addr");
    phi->addIncoming(okAddress, okBlock);
    phi->addIncoming(builder_->CreatePtrToInt(builder_->CreateIntToPtr(i64(0), ptrType_), i64Type_),
                     oobBlock);
    return phi;
  }

  if (objectType != nullptr && objectType->kind == TypeKind::Struct) {
    for (std::size_t index = 0; index < objectType->fields.size(); ++index) {
      if (objectType->fields[index].first == member.property) {
        return builder_->CreateAdd(base, i64(memberOffset(objectType, index)));
      }
    }
  }

  throw errors::ErrorException{errors::ErrorStatus{
      errors::ErrorCode::TYPE_MEMBER_ACCESS_NOT_SUPPORTED, member.property, range}};
}

IrGenerator::EmitValue IrGenerator::generateCall(const reader::ExprPtr &expression,
                                                 const reader::CallExpr &call) {
  if (const auto *identifier = std::get_if<reader::IdentifierExpr>(&call.callee->node)) {
    const std::string &name = identifier->name;
    // Math builtins handled directly for inline comparison/select operations
    if (name == "min" || name == "max" || name == "abs" || name == "floor" ||
        name == "ceil" || name == "round") {
      if (call.arguments.size() != 1 && call.arguments.size() != 2) {
        lastValue_ = rawValue(i64(0));
        return lastValue_;
      }
      if (name == "min" || name == "max") {
        if (call.arguments.size() != 2) {
          lastValue_ = rawValue(i64(0));
          return lastValue_;
        }
        EmitValue left = generateExpression(call.arguments[0]);
        EmitValue right = generateExpression(call.arguments[1]);
        TypePtr leftType = typeOf(call.arguments[0]);
        TypePtr rightType = typeOf(call.arguments[1]);
        right = coerce(leftType, rightType, right);
        llvm::Value *lv = left.value;
        llvm::Value *rv = right.value;
        llvm::Value *cmp = builder_->CreateFCmpOLT(lv, rv);
        llvm::Value *selected = builder_->CreateSelect(cmp,
            name == "min" ? lv : rv,
            name == "min" ? rv : lv);
        lastValue_ = EmitValue{selected, true};
        return lastValue_;
      } else if (name == "abs") {
        EmitValue arg = generateExpression(call.arguments[0]);
        llvm::Value *av = arg.value;
        llvm::Value *zero = llvm::ConstantFP::get(doubleType_, 0.0);
        llvm::Value *cmp = builder_->CreateFCmpOLT(av, zero);
        llvm::Value *negated = builder_->CreateFNeg(av);
        llvm::Value *selected = builder_->CreateSelect(cmp, negated, av);
        lastValue_ = EmitValue{selected, true};
        return lastValue_;
      } else if (name == "floor") {
        EmitValue arg = generateExpression(call.arguments[0]);
        llvm::Value *av = arg.value;
        llvm::Value *trunc = builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat, {i64Type_, doubleType_}, {av});
        llvm::Value *truncD = builder_->CreateSIToFP(trunc, doubleType_);
        llvm::Value *isNeg = builder_->CreateFCmpOLT(av, llvm::ConstantFP::get(doubleType_, 0.0));
        llvm::Value *hasFrac = builder_->CreateFCmpONE(av, truncD);
        llvm::Value *adjust = builder_->CreateSelect(
            builder_->CreateAnd(isNeg, hasFrac),
            llvm::ConstantFP::get(doubleType_, 1.0),
            llvm::ConstantFP::get(doubleType_, 0.0));
        llvm::Value *floored = builder_->CreateFSub(truncD, adjust);
        lastValue_ = EmitValue{floored, true};
        return lastValue_;
      } else if (name == "ceil") {
        EmitValue arg = generateExpression(call.arguments[0]);
        llvm::Value *av = arg.value;
        llvm::Value *trunc = builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat, {i64Type_, doubleType_}, {av});
        llvm::Value *truncD = builder_->CreateSIToFP(trunc, doubleType_);
        llvm::Value *isPos = builder_->CreateFCmpOGT(av, llvm::ConstantFP::get(doubleType_, 0.0));
        llvm::Value *hasFrac = builder_->CreateFCmpONE(av, truncD);
        llvm::Value *adjust = builder_->CreateSelect(
            builder_->CreateAnd(isPos, hasFrac),
            llvm::ConstantFP::get(doubleType_, 1.0),
            llvm::ConstantFP::get(doubleType_, 0.0));
        llvm::Value *ceiled = builder_->CreateFAdd(truncD, adjust);
        lastValue_ = EmitValue{ceiled, true};
        return lastValue_;
      } else if (name == "round") {
        EmitValue arg = generateExpression(call.arguments[0]);
        llvm::Value *av = arg.value;
        llvm::Value *plus = builder_->CreateFAdd(av, llvm::ConstantFP::get(doubleType_, 0.5));
        llvm::Value *trunc = builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat, {i64Type_, doubleType_}, {plus});
        llvm::Value *truncD = builder_->CreateSIToFP(trunc, doubleType_);
        llvm::Value *isNeg = builder_->CreateFCmpOLT(plus, llvm::ConstantFP::get(doubleType_, 0.0));
        llvm::Value *hasFrac = builder_->CreateFCmpONE(plus, truncD);
        llvm::Value *adjust = builder_->CreateSelect(
            builder_->CreateAnd(isNeg, hasFrac),
            llvm::ConstantFP::get(doubleType_, 1.0),
            llvm::ConstantFP::get(doubleType_, 0.0));
        llvm::Value *rounded = builder_->CreateFSub(truncD, adjust);
        lastValue_ = EmitValue{rounded, true};
        return lastValue_;
      }
    }
    if (name == "assert") {
      if (call.arguments.size() < 1 || call.arguments.size() > 2) {
        lastValue_ = rawValue(i64(0));
        return lastValue_;
      }
      EmitValue cond = generateExpression(call.arguments[0]);
      TypePtr condType = typeOf(call.arguments[0]);
      llvm::Value *condVal;
      if (condType != nullptr && isNumber(condType)) {
        condVal = builder_->CreateFCmpONE(cond.value, llvm::ConstantFP::get(doubleType_, 0.0));
      } else {
        condVal = builder_->CreateICmpNE(cond.value, llvm::ConstantInt::get(i64Type_, 0));
      }
      llvm::BasicBlock *assertOk = createBlock("assert.ok");
      llvm::BasicBlock *assertFail = createBlock("assert.fail");
      llvm::BasicBlock *assertEnd = createBlock("assert.end");
      builder_->CreateCondBr(condVal, assertOk, assertFail);
      builder_->SetInsertPoint(assertFail);
      if (call.arguments.size() == 2) {
        // Print message
        (void)generateExpression(call.arguments[1]);
      }
      builder_->CreateCall(boundsFunction());
      builder_->CreateUnreachable();
      builder_->SetInsertPoint(assertOk);
      builder_->CreateBr(assertEnd);
      builder_->SetInsertPoint(assertEnd);
      lastValue_ = rawValue(i64(0));
      return lastValue_;
    }
    if (const builtins::BuiltinSpec *builtin =
            builtins::BuiltinRegistry::instance().find(identifier->name)) {
      builtin->emit(*this, call);
      return lastValue_;
    }
  }
  const TypePtr target = resolve(annotations_->argumentTypes.at(expression.get()));
  if (target == nullptr || target->kind != TypeKind::Function)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::TYPE_CALL_TARGET_EXPECTS_ARGUMENTS, "call", expression->range}};
  EmitValue callee = generateExpression(call.callee);
  llvm::Value *functionPointer =
      builder_->CreateIntToPtr(asRaw(callee), llvm::PointerType::getUnqual(*context_));
  std::vector<llvm::Value *> arguments;
  const std::size_t fixedParameters = target->parameters.size();
  bool isVariadic = target->isVariadic;
  std::vector<llvm::Value *> rawArgs;
  rawArgs.reserve(call.arguments.size());
  for (std::size_t index = 0; index < call.arguments.size(); ++index) {
    EmitValue argument = generateExpression(call.arguments[index]);
    TypePtr argumentType = typeOf(call.arguments[index]);
    if (index < fixedParameters) {
      argument = coerce(target->parameters[index], argumentType, argument);
    }
    // For variadic args stored in shadow array, convert double to i64 using FPToSI
    // (not bitcast) so the callee can correctly interpret via SIToFP/UIToFP.
    llvm::Value *rawArg;
    if (index >= fixedParameters && argument.number) {
      rawArg = builder_->CreateIntrinsic(llvm::Intrinsic::fptosi_sat,
                                         {i64Type_, doubleType_}, {argument.value});
    } else {
      rawArg = asRaw(argument);
    }
    rawArgs.push_back(rawArg);
    arguments.push_back(rawArg);
  }
  if (isVariadic) {
    // Count i64 slots: bytes = 2 slots (ptr + len), everything else = 1 slot
    std::size_t slotCount = 0;
    for (std::size_t index = fixedParameters; index < call.arguments.size(); ++index) {
      TypePtr argType = typeOf(call.arguments[index]);
      if (argType && isBytes(argType)) slotCount += 2;
      else slotCount += 1;
    }
    // Allocate shadow array with slotCount entries
    llvm::Value *shadowArray = builder_->CreateAlloca(i64Type_, i64(slotCount));
    std::size_t slotIndex = 0;
    for (std::size_t i = fixedParameters; i < call.arguments.size(); ++i) {
      TypePtr argType = typeOf(call.arguments[i]);
      llvm::Value *rawArg = rawArgs[i];
      if (argType && isBytes(argType)) {
        // Store pointer (slot) and length (slot + 1)
        llvm::Value *elemPtr = builder_->CreateGEP(i64Type_, shadowArray, {i64(slotIndex)});
        builder_->CreateStore(rawArg, elemPtr);
        slotIndex++;
        // Length is at pointer + 8 in the bytes descriptor
        llvm::Value *lenPtr =
            builder_->CreateGEP(i8Type_, builder_->CreateIntToPtr(rawArg, ptrType_), i64(8));
        llvm::Value *lenVal = builder_->CreateLoad(i64Type_, lenPtr);
        elemPtr = builder_->CreateGEP(i64Type_, shadowArray, {i64(slotIndex)});
        builder_->CreateStore(lenVal, elemPtr);
        slotIndex++;
      } else {
        llvm::Value *elemPtr = builder_->CreateGEP(i64Type_, shadowArray, {i64(slotIndex)});
        builder_->CreateStore(rawArg, elemPtr);
        slotIndex++;
      }
    }
    // Insert hidden params right after fixed JSmm params
    arguments.insert(arguments.begin() + fixedParameters,
                     i64(slotCount));    // hidden va_count (slot count)
    arguments.insert(arguments.begin() + fixedParameters + 1,
                     builder_->CreateBitCast(shadowArray, ptrType_)); // hidden va_array pointer
  }
  // Build call parameter types: fixed JSmm params are i64, va_count is i64,
  // va_array pointer is ptr. Variadic args go into ... (not fixed params).
  std::vector<llvm::Type *> callParamTypes;
  callParamTypes.reserve(fixedParameters + (isVariadic ? 2 : 0));
  for (std::size_t index = 0; index < fixedParameters; ++index)
    callParamTypes.push_back(i64Type_);
  if (isVariadic) {
    callParamTypes.push_back(i64Type_);   // va_count
    callParamTypes.push_back(ptrType_);   // va_array pointer
  }
  llvm::FunctionType *functionType = llvm::FunctionType::get(
      i64Type_, callParamTypes, isVariadic);
  return asTyped(builder_->CreateCall(functionType, functionPointer, arguments), target->result);
}

LoweredModule IrGenerator::generate(const reader::Program &program,
                                    const types::Annotations &annotations,
                                    const types::TypeContext &typeContext) {
  auto ownedContext = std::make_shared<llvm::LLVMContext>();
  context_ = ownedContext.get();
  module_ = new llvm::Module("jsmm", *context_);
  module_->setTargetTriple(llvm::Triple(llvm::sys::getDefaultTargetTriple()));

  builder_ = std::make_unique<llvm::IRBuilder<>>(*context_);
  annotations_ = &annotations;
  types_ = const_cast<types::TypeContext *>(&typeContext);
  doubleType_ = llvm::Type::getDoubleTy(*context_);
  i64Type_ = llvm::Type::getInt64Ty(*context_);
  i32Type_ = llvm::Type::getInt32Ty(*context_);
  i8Type_ = llvm::Type::getInt8Ty(*context_);
  ptrType_ = llvm::PointerType::getUnqual(*context_);
  voidType_ = llvm::Type::getVoidTy(*context_);
  globals_.clear();
  functions_.clear();
  strings_.clear();
  runtime_.clear();
  emptyString_ = internString("");
  scopes_.clear();
  slotAllocas_.clear();
  loops_.clear();
  labels_.clear();
  pendingLoopLabel_.clear();
  slotCount_ = 0;

  for (const auto &statement : program.statements) {
    if (const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement->node)) {
      std::size_t jsmmFixedCount = 0;
      for (const auto &param : declaration->parameters)
        if (!param.isVariadic) ++jsmmFixedCount;
      std::vector<llvm::Type *> parameters(jsmmFixedCount, i64Type_);
      bool isVariadic = false;
      for (const auto &param : declaration->parameters) {
        if (param.isVariadic) {
          isVariadic = true;
          break;
        }
      }
      if (isVariadic) {
        parameters.push_back(i64Type_);   // hidden va_count
        parameters.push_back(ptrType_);   // hidden va_array pointer
      }
      llvm::FunctionType *signature = llvm::FunctionType::get(i64Type_, parameters, isVariadic);
      llvm::Function *function = llvm::Function::Create(signature, llvm::Function::ExternalLinkage,
                                                        declaration->name, *module_);
      function->setDSOLocal(true);
      functions_[declaration->name] = function;
    }
  }
  generateEntry(program);
  for (const auto &statement : program.statements) {
    if (const auto *declaration = std::get_if<reader::FunctionDeclaration>(&statement->node))
      generateFunction(*declaration, functions_.at(declaration->name));
  }
  std::string verification;
  llvm::raw_string_ostream output(verification);
  if (llvm::verifyModule(*module_, &output))
    throw std::runtime_error("invalid generated LLVM IR: " + output.str());
  LoweredModule result{ownedContext, std::unique_ptr<llvm::Module>(module_)};
  module_ = nullptr;
  context_ = nullptr;
  return result;
}

void IrGenerator::generateEntry(const reader::Program &program) {
  llvm::FunctionType *signature = llvm::FunctionType::get(i64Type_, false);
  currentFunction_ =
      llvm::Function::Create(signature, llvm::Function::ExternalLinkage, "jsmm_entry", *module_);
  auto *entry = llvm::BasicBlock::Create(*context_, "entry", currentFunction_);
  builder_->SetInsertPoint(entry);
  returnType_ = types_->nullType();
  returnSlot_ = createEntryAlloca();
  builder_->CreateStore(asRaw(emitZeroValue(returnType_)), returnSlot_);
  returnBlock_ = createBlock("return");
  generatingEntry_ = true;
  beginScope();
  generateStatements(program.statements);
  if (!terminated()) builder_->CreateBr(returnBlock_);
  builder_->SetInsertPoint(returnBlock_);
  if (!terminated()) builder_->CreateRet(builder_->CreateLoad(i64Type_, returnSlot_));
  endScope();
  generatingEntry_ = false;
  currentFunction_ = nullptr;
}

void IrGenerator::generateFunction(const reader::FunctionDeclaration &declaration,
                                   llvm::Function *function) {
  currentFunction_ = function;
  llvm::BasicBlock *entry = llvm::BasicBlock::Create(*context_, "entry", function);
  builder_->SetInsertPoint(entry);
  scopes_.clear();
  slotAllocas_.clear();
  loops_.clear();
  labels_.clear();
  pendingLoopLabel_.clear();
  slotCount_ = 0;
  vaCountSlot_ = nullptr;
  vaCountValue_ = nullptr;
  vaArraySlot_ = nullptr;
  const auto declaredFunction = annotations_->functionTypes.find(declaration.name);
  const TypePtr functionType =
      declaredFunction != annotations_->functionTypes.end() ? declaredFunction->second : nullptr;
  returnType_ = functionType != nullptr ? resolve(functionType->result) : types_->nullType();
  if (returnType_ != nullptr && returnType_->kind == TypeKind::Variable)
    returnType_ = types_->nullType();
  returnSlot_ = createEntryAlloca();
  builder_->CreateStore(asRaw(emitZeroValue(returnType_)), returnSlot_);
  returnBlock_ = createBlock("return");
  beginScope();
  auto argument = function->arg_begin();
  std::size_t parameterIndex = 0;
  std::size_t jsmmFixedCount = 0;
  for (const auto &parameter : declaration.parameters)
    if (!parameter.isVariadic) ++jsmmFixedCount;

  // Store JSmm fixed parameters
  for (std::size_t i = 0; i < jsmmFixedCount; ++i) {
    const TypePtr parameterType =
        functionType != nullptr && parameterIndex < functionType->parameters.size()
            ? resolve(functionType->parameters[parameterIndex])
            : nullptr;
    llvm::Value *address = slotAlloca(allocateSlot(), parameterType);
    const reader::FunctionParameter &param = declaration.parameters[i];
    scopes_.back().slots.emplace(param.name, Slot{address});
    storeSlot(address, asTyped(&*argument, parameterType), parameterType);
    ++argument;
    ++parameterIndex;
  }

  // Variadic prologue: store hidden va_count and va_array pointer
  if (functionType != nullptr && functionType->isVariadic) {
    auto argument = function->arg_begin();
    // Skip fixed JSmm params to reach va_count
    for (std::size_t i = 0; i < jsmmFixedCount; ++i)
        ++argument;
    llvm::Value *vaCountVal = &*argument;
    vaCountValue_ = vaCountVal;
    ++argument;
    vaCountSlot_ = builder_->CreateAlloca(i64Type_);
    vaCountSlot_->setName("va.count");
    builder_->CreateStore(vaCountVal, vaCountSlot_);

    vaArraySlot_ = &*argument;
    ++argument;
  }
  generateStatements(declaration.body->statements);
  if (!terminated()) builder_->CreateBr(returnBlock_);
  builder_->SetInsertPoint(returnBlock_);
  if (!terminated()) builder_->CreateRet(builder_->CreateLoad(i64Type_, returnSlot_));
  endScope();
  currentFunction_ = nullptr;
}

void IrGenerator::emitArgument(const reader::ExprPtr &argument) {
  lastValue_ = argument ? generateExpression(argument) : rawValue(i64(0));
}
void IrGenerator::moveResultToArgumentRegister() {}
void IrGenerator::clearResult() { lastValue_ = rawValue(i64(0)); }
types::TypePtr IrGenerator::operandType(const reader::ExprPtr &operand) { return typeOf(operand); }
void IrGenerator::emitNumberResult(double value) { lastValue_ = numberValue(value); }

void IrGenerator::emitAllocation(const reader::ExprPtr &size) {
  EmitValue amount = generateExpression(size);
  llvm::Value *result = builder_->CreateCall(allocFunction(), {toInt64(amount)});
  // Check for allocation failure (null return)
  llvm::Value *isNull = builder_->CreateICmpEQ(result, i64(0));
  llvm::BasicBlock *allocOk = createBlock("alloc.ok");
  llvm::BasicBlock *allocFail = createBlock("alloc.fail");
  llvm::BasicBlock *allocEnd = createBlock("alloc.end");
  builder_->CreateCondBr(isNull, allocFail, allocOk);
  builder_->SetInsertPoint(allocFail);
  builder_->CreateCall(boundsFunction());
  builder_->CreateUnreachable();
  builder_->SetInsertPoint(allocOk);
  builder_->CreateBr(allocEnd);
  builder_->SetInsertPoint(allocEnd);
  lastValue_ = rawValue(result);
}

void IrGenerator::emitVoidResult() { lastValue_ = rawValue(i64(0)); }

void IrGenerator::emitFree(const reader::ExprPtr &ptr) {
  EmitValue p = generateExpression(ptr);
  // free is a no-op in the arena allocator (reclaimed at process exit),
  // but we still evaluate the argument for side effects and type-checking.
  (void)p;
  lastValue_ = rawValue(i64(0));
}

IrGenerator::EmitValue IrGenerator::coerceI64ToType(llvm::Value *i64val, types::TypePtr type) {
  type = resolve(type);
  if (type == nullptr) return rawValue(i64val);
  switch (type->kind) {
  case TypeKind::Pointer: return rawValue(i64val);
  case TypeKind::Number:
  case TypeKind::Byte:
  case TypeKind::Integer: {
    const bool signedNum =
        type->kind == TypeKind::Byte || (type->kind == TypeKind::Integer && type->isSigned);
    return EmitValue{
        signedNum ? builder_->CreateSIToFP(i64val, doubleType_)
                  : builder_->CreateUIToFP(i64val, doubleType_),
        true};
  }
  case TypeKind::Boolean:
    return EmitValue{builder_->CreateSIToFP(i64val, doubleType_), true};
  default:
    return rawValue(i64val);
  }
}

void IrGenerator::emitVaCount() {
  if (vaCountValue_ == nullptr) {
    lastValue_ = numberValue(0.0);
    return;
  }
  lastValue_ = EmitValue{builder_->CreateSIToFP(vaCountValue_, doubleType_), true};
}

void IrGenerator::emitVaArg(const reader::CallExpr &call) {
  if (vaArraySlot_ == nullptr || vaCountValue_ == nullptr) {
    lastValue_ = rawValue(i64(0));
    return;
  }
  if (call.arguments.size() != 2) {
    lastValue_ = rawValue(i64(0));
    return;
  }
  EmitValue idx = generateExpression(call.arguments[0]);
  llvm::Value *indexVal = toInt64(idx);

  // Look up the resolved type of the type operand (TypeOperandExpr)
  TypePtr resultType = annotations_->expressionTypes.at(call.arguments[1].get());

  // Bounds check: index < va_count
  llvm::Value *count = builder_->CreateLoad(i64Type_, vaCountSlot_);
  llvm::Value *oob = builder_->CreateICmpUGE(indexVal, count);
  llvm::BasicBlock *oobBlock = createBlock("va_arg.oob");
  llvm::BasicBlock *okBlock = createBlock("va_arg.ok");
  llvm::BasicBlock *endBlock = createBlock("va_arg.end");
  builder_->CreateCondBr(oob, oobBlock, okBlock);

  builder_->SetInsertPoint(oobBlock);
  builder_->CreateCall(boundsFunction());
  builder_->CreateUnreachable();

  builder_->SetInsertPoint(okBlock);
  llvm::Value *elemPtr = builder_->CreateGEP(i64Type_, vaArraySlot_, {indexVal});
  llvm::Value *loaded = builder_->CreateLoad(i64Type_, elemPtr);
  lastValue_ = coerceI64ToType(loaded, resultType);
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(endBlock);
}

void IrGenerator::emitLength(const reader::ExprPtr &operand) {
  EmitValue base = generateExpression(operand);
  llvm::Value *basePtr = asRaw(base);
  llvm::Value *len = builder_->CreateLoad(i64Type_, bytePointer(basePtr, 0));
  lastValue_ = EmitValue{builder_->CreateSIToFP(len, doubleType_), true};
}

void IrGenerator::emitMemcpy(const reader::ExprPtr &dest, const reader::ExprPtr &src,
                             const reader::ExprPtr &count) {
  EmitValue destination = generateExpression(dest);
  EmitValue source = generateExpression(src);
  EmitValue length = generateExpression(count);
  llvm::Value *total = toInt64(length);
  llvm::Value *destinationBase = asRaw(destination);
  llvm::Value *sourceBase = asRaw(source);
  llvm::Value *result =
      builder_->CreateCall(memcpyFunction(), {destinationBase, sourceBase, total});
  lastValue_ = rawValue(result);
}

void IrGenerator::emitMemset(const reader::ExprPtr &dest, const reader::ExprPtr &value,
                             const reader::ExprPtr &count) {
  EmitValue destination = generateExpression(dest);
  EmitValue fill = generateExpression(value);
  EmitValue length = generateExpression(count);
  llvm::Value *total = toInt64(length);
  llvm::Value *destinationBase = asRaw(destination);
  llvm::Value *fillValue = toInt64(fill);
  llvm::Value *result =
      builder_->CreateCall(memsetFunction(), {destinationBase, fillValue, total});
  lastValue_ = rawValue(result);
}

void IrGenerator::emitMemcmp(const reader::ExprPtr &a, const reader::ExprPtr &b,
                             const reader::ExprPtr &count) {
  EmitValue baseA = generateExpression(a);
  EmitValue baseB = generateExpression(b);
  EmitValue length = generateExpression(count);
  llvm::Value *total = toInt64(length);
  llvm::Value *aBase = asRaw(baseA);
  llvm::Value *bBase = asRaw(baseB);
  llvm::Value *result =
      builder_->CreateCall(memcmpFunction(), {aBase, bBase, total});
  lastValue_ = EmitValue{builder_->CreateSIToFP(result, doubleType_), true};
}
void IrGenerator::declareHelper(std::string, std::string) {}
void IrGenerator::useHelper(const std::string &) {}
void IrGenerator::callHelper(const std::string &) {}

void IrGenerator::formatValue(const reader::ExprPtr &expression) {
  IrGenerator::EmitValue value = generateExpression(expression);
  emitFormatValue(value, typeOf(expression));
  lastValue_ = rawValue(i64(0));
}
void IrGenerator::formatArray(const reader::ExprPtr &expression) {
  IrGenerator::EmitValue value = generateExpression(expression);
  emitFormatArray(value, typeOf(expression));
  lastValue_ = rawValue(i64(0));
}
void IrGenerator::formatNewline() {
  builder_->CreateCall(stringWriter(), {builder_->CreatePtrToInt(internString("\n"), i64Type_)});
}

void IrGenerator::emitFormatValue(IrGenerator::EmitValue value, types::TypePtr type) {
  type = resolve(type);
  if (type == nullptr) {
    builder_->CreateCall(nullWriter());
    return;
  }
  switch (type->kind) {
  case TypeKind::Number:
  case TypeKind::Integer: builder_->CreateCall(numberWriter(), {value.value}); break;
  case TypeKind::String: builder_->CreateCall(stringWriter(), {asRaw(value)}); break;
  case TypeKind::Boolean: builder_->CreateCall(boolWriter(), {asRaw(value)}); break;
  case TypeKind::Byte: builder_->CreateCall(numberWriter(), {value.value}); break;
  case TypeKind::Bytes: builder_->CreateCall(stringWriter(), {asRaw(value)}); break;
  case TypeKind::Null: builder_->CreateCall(nullWriter()); break;
  case TypeKind::Array: emitFormatArray(value, type); break;
  case TypeKind::Pointer:
    builder_->CreateCall(numberWriter(), {builder_->CreateSIToFP(asRaw(value), doubleType_)});
    break;
  case TypeKind::Union: emitFormatUnion(value, type); break;
  case TypeKind::Struct: builder_->CreateCall(stringWriter(), {asRaw(value)}); break;
  case TypeKind::Function: builder_->CreateCall(stringWriter(), {asRaw(value)}); break;
  case TypeKind::Never: builder_->CreateCall(nullWriter()); break;
  case TypeKind::Variable: builder_->CreateCall(nullWriter()); break;
  }
}
void IrGenerator::emitFormatArray(IrGenerator::EmitValue value, types::TypePtr type) {
  builder_->CreateCall(lbracketWriter());
  llvm::Value *base = asRaw(value);
  llvm::Value *length = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
  llvm::BasicBlock *cond = createBlock("array.print.cond");
  llvm::BasicBlock *body = createBlock("array.print.body");
  llvm::BasicBlock *end = createBlock("array.print.end");
  builder_->CreateBr(cond);
  builder_->SetInsertPoint(cond);
  auto *index = builder_->CreatePHI(i64Type_, 2, "array.index");
  index->addIncoming(i64(0), builder_->GetInsertBlock()->getSinglePredecessor());
  builder_->CreateCondBr(builder_->CreateICmpULT(index, length), body, end);
  builder_->SetInsertPoint(body);
  llvm::Value *notFirst = builder_->CreateICmpNE(index, i64(0));
  llvm::BasicBlock *separatorBlock = createBlock("array.print.separator");
  llvm::BasicBlock *itemBlock = createBlock("array.print.item");
  builder_->CreateCondBr(notFirst, separatorBlock, itemBlock);
  builder_->SetInsertPoint(separatorBlock);
  builder_->CreateCall(separatorWriter());
  builder_->CreateBr(itemBlock);
  builder_->SetInsertPoint(itemBlock);
  const std::uint64_t stride = elementSize(type->element);
  IrGenerator::EmitValue item = loadSlot(
      dynamicPointer(base, builder_->CreateAdd(i64(8), builder_->CreateMul(index, i64(stride)))),
      type->element);
  emitFormatValue(item, type->element);
  llvm::Value *next = builder_->CreateAdd(index, i64(1));
  llvm::BasicBlock *bodyEnd = builder_->GetInsertBlock();
  if (!terminated()) builder_->CreateBr(cond);
  index->addIncoming(next, bodyEnd);
  builder_->SetInsertPoint(end);
  builder_->CreateCall(rbracketWriter());
}
void IrGenerator::emitFormatUnion(IrGenerator::EmitValue value, types::TypePtr type) {
  llvm::Value *base = asRaw(value);
  llvm::Value *tag = builder_->CreateLoad(i64Type_, bytePointer(base, 0));
  llvm::Value *payload = builder_->CreateLoad(i64Type_, bytePointer(base, 8));
  const TypePtr payloadType = unionPayload(type);
  const std::string suffix = std::to_string(unionBlockCounter_++);

  llvm::BasicBlock *nullBlock = createBlock("union.null" + suffix);
  llvm::BasicBlock *numberCheck = createBlock("union.check.number" + suffix);
  llvm::BasicBlock *numberBlock = createBlock("union.number" + suffix);
  llvm::BasicBlock *stringCheck = createBlock("union.check.string" + suffix);
  llvm::BasicBlock *stringBlock = createBlock("union.string" + suffix);
  llvm::BasicBlock *booleanCheck = createBlock("union.check.boolean" + suffix);
  llvm::BasicBlock *booleanBlock = createBlock("union.boolean" + suffix);
  llvm::BasicBlock *payloadBlock = createBlock("union.payload" + suffix);
  llvm::BasicBlock *endBlock = createBlock("union.end" + suffix);

  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagNull)), nullBlock, numberCheck);

  builder_->SetInsertPoint(nullBlock);
  builder_->CreateCall(nullWriter());
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(numberCheck);
  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagNumber)), numberBlock,
                         stringCheck);

  builder_->SetInsertPoint(numberBlock);
  builder_->CreateCall(numberWriter(), {builder_->CreateBitCast(payload, doubleType_)});
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(stringCheck);
  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagString)), stringBlock,
                         booleanCheck);

  builder_->SetInsertPoint(stringBlock);
  builder_->CreateCall(stringWriter(), {payload});
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(booleanCheck);
  builder_->CreateCondBr(builder_->CreateICmpEQ(tag, i64(unionTagBoolean)), booleanBlock,
                         payloadBlock);

  builder_->SetInsertPoint(booleanBlock);
  builder_->CreateCall(boolWriter(), {payload});
  builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(payloadBlock);
  emitFormatValue(asTyped(payload, payloadType), payloadType);
  if (!terminated()) builder_->CreateBr(endBlock);

  builder_->SetInsertPoint(endBlock);
}

} // namespace jsmm::emit
