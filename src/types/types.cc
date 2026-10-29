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
  std::sort(fields.begin(), fields.end(),
            [](const auto &left, const auto &right) { return left.first < right.first; });
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Struct;
  type->id = nextId_++;
  type->fields = std::move(fields);
  return type;
}

TypePtr TypeContext::arrayType(TypePtr element) {
  auto type = std::make_shared<Type>();
  type->kind = TypeKind::Array;
  type->id = nextId_++;
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
  case TypeKind::Number: return "number";
  case TypeKind::String: return "string";
  case TypeKind::Boolean: return "boolean";
  case TypeKind::Byte: return "byte";
  case TypeKind::Bytes: return "bytes";
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

} // namespace jsmm::types
