#include "types/types.hh"

#include "errors/errors.hh"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace jsmm::types {

TypeContext::TypeContext() : nextId_(0) {
  number_ = std::make_shared<Type>();
  number_->kind = TypeKind::Number;
  number_->id = nextId_++;
  string_ = std::make_shared<Type>();
  string_->kind = TypeKind::String;
  string_->id = nextId_++;
  boolean_ = std::make_shared<Type>();
  boolean_->kind = TypeKind::Boolean;
  boolean_->id = nextId_++;
  byte_ = std::make_shared<Type>();
  byte_->kind = TypeKind::Byte;
  byte_->id = nextId_++;
  bytes_ = std::make_shared<Type>();
  bytes_->kind = TypeKind::Bytes;
  bytes_->id = nextId_++;
  null_ = std::make_shared<Type>();
  null_->kind = TypeKind::Null;
  null_->id = nextId_++;
  never_ = std::make_shared<Type>();
  never_->kind = TypeKind::Never;
  never_->id = nextId_++;
}

TypePtr TypeContext::number() const { return number_; }

TypePtr TypeContext::string() const { return string_; }

TypePtr TypeContext::boolean() const { return boolean_; }

TypePtr TypeContext::byteType() const { return byte_; }

TypePtr TypeContext::bytesType() const { return bytes_; }

TypePtr TypeContext::integerType(std::size_t bits, bool isSigned) {
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Integer;
  type->id = nextId_++;
  type->length = bits;
  type->isSigned = isSigned;
  return type;
}

TypePtr TypeContext::nullType() const { return null_; }

TypePtr TypeContext::never() const { return never_; }

TypePtr TypeContext::variable() {
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Variable;
  type->id = nextId_++;
  return type;
}

TypePtr TypeContext::functionType(std::vector<TypePtr> parameters, TypePtr result) {
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Function;
  type->id = nextId_++;
  type->parameters = std::move(parameters);
  type->result = std::move(result);
  return type;
}

TypePtr TypeContext::structType(std::vector<std::pair<std::string, TypePtr>> fields) {
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Struct;
  type->id = nextId_++;
  type->fields = std::move(fields);
  return type;
}

TypePtr TypeContext::arrayType(TypePtr element, std::size_t length) {
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Array;
  type->id = nextId_++;
  type->length = length;
  type->element = std::move(element);
  return type;
}

TypePtr TypeContext::unionType(std::vector<TypePtr> members) {
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Union;
  type->id = nextId_++;
  type->members = std::move(members);
  return type;
}

TypePtr TypeContext::pointerType(TypePtr pointed) {
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Pointer;
  type->id = nextId_++;
  type->pointed = std::move(pointed);
  return type;
}

TypePtr TypeContext::resolve(TypePtr type) const {
  std::set<int> seen;
  while (type != nullptr && type->kind == TypeKind::Variable && type->link != nullptr) {
    if (!seen.insert(type->id).second)
      throw errors::ErrorException{
          errors::ErrorStatus{errors::ErrorCode::UNIFY_TYPE_CYCLE_DETECTED, {}, {}}};
    type = type->link;
  }
  return type;
}

std::string TypeContext::show(TypePtr type) {
  showCounter_ = 0;
  return showType(std::move(type));
}

std::string TypeContext::showType(TypePtr type) {
  type = resolve(type);
  if (type == nullptr) return "?";
  switch (type->kind) {
  case TypeKind::Number: return "f64";
  case TypeKind::String: return "string";
  case TypeKind::Boolean: return "boolean";
  case TypeKind::Byte: return "byte";
  case TypeKind::Bytes: return "bytes";
  case TypeKind::Integer:
    return "int(" + std::to_string(type->length) + ", " +
           (type->isSigned ? "signed" : "unsigned") + ")";
  case TypeKind::Null: return "null";
  case TypeKind::Never: return "never";
  case TypeKind::Variable: return "t" + std::to_string(++showCounter_);
  case TypeKind::Function: {
    std::string out = "(";
    for (std::size_t i = 0; i < type->parameters.size(); ++i) {
      if (i != 0) out += ", ";
      out += showType(type->parameters[i]);
    }
    out += ") -> ";
    out += showType(type->result);
    return out;
  }
  case TypeKind::Struct: {
    std::string out = "struct ObjectLiteral { ";
    for (std::size_t i = 0; i < type->fields.size(); ++i) {
      if (i != 0) out += ", ";
      out += type->fields[i].first;
      out += ": ";
      out += showType(type->fields[i].second);
    }
    out += " }";
    return out;
  }
  case TypeKind::Array: return showType(type->element) + "[]";
  case TypeKind::Pointer: return showType(type->pointed) + "*";
  case TypeKind::Union: {
    std::string out;
    for (std::size_t i = 0; i < type->members.size(); ++i) {
      if (i != 0) out += " | ";
      out += showType(type->members[i]);
    }
    return out;
  }
  }
  return "?";
}

namespace {

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
  if (alignment <= 1) return value;
  return (value + alignment - 1) / alignment * alignment;
}

std::uint64_t floorPowerOfTwo(std::uint64_t value) {
  std::uint64_t result = 1;
  while (result * 2 <= value) result *= 2;
  return result;
}

TypeLayout knownLayout(std::uint64_t size, std::uint64_t alignment) {
  return TypeLayout{size, alignment, true};
}

TypePtr followLinks(TypePtr type) {
  while (type != nullptr && type->kind == TypeKind::Variable && type->link != nullptr)
    type = type->link;
  return type;
}

} // namespace

TypeLayout layoutOf(TypePtr type) {
  type = followLinks(std::move(type));
  if (type == nullptr) return {};
  switch (type->kind) {
  case TypeKind::Byte:
  case TypeKind::Boolean: return knownLayout(1, 1);
  case TypeKind::Integer: {
    if (type->length == 0) return {};
    const std::uint64_t size = (type->length + 7) / 8;  // round up to whole bytes
    return knownLayout(size, size < 8 ? floorPowerOfTwo(size) : 8);
  }
  case TypeKind::Number:
  case TypeKind::String:
  case TypeKind::Bytes:
  case TypeKind::Pointer:
  case TypeKind::Function: return knownLayout(8, 8);
  case TypeKind::Array: {
    if (type->length == 0) return {};
    const TypeLayout element = layoutOf(type->element);
    if (!element.defined) return {};
    return knownLayout(element.size * type->length, element.alignment);
  }
  case TypeKind::Struct: {
    std::uint64_t offset = 0;
    std::uint64_t alignment = 1;
    for (const auto &field : type->fields) {
      const TypeLayout fieldLayout = layoutOf(field.second);
      if (!fieldLayout.defined) return {};
      offset = alignUp(offset, fieldLayout.alignment) + fieldLayout.size;
      if (fieldLayout.alignment > alignment) alignment = fieldLayout.alignment;
    }
    return knownLayout(alignUp(offset, alignment), alignment);
  }
  default: return {};
  }
}

std::uint64_t fieldOffset(TypePtr type, std::size_t index) {
  type = followLinks(std::move(type));
  if (type == nullptr || type->kind != TypeKind::Struct) return 0;
  std::uint64_t offset = 0;
  for (std::size_t position = 0; position < type->fields.size(); ++position) {
    const TypeLayout fieldLayout = layoutOf(type->fields[position].second);
    if (!fieldLayout.defined) return 0;
    offset = alignUp(offset, fieldLayout.alignment);
    if (position == index) return offset;
    offset += fieldLayout.size;
  }
  return 0;
}

} // namespace jsmm::types
