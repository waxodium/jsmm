#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace edon::ecma {

struct Undefined final {};
struct Null final {};
struct Symbol final {
  explicit Symbol(std::string description = {}) : description(std::move(description)) {}
  std::string description;
};

class Value;
struct Object;
using ObjectPtr = std::shared_ptr<Object>;
using SymbolPtr = std::shared_ptr<Symbol>;

class Value {
public:
  using Storage = std::variant<Undefined, Null, bool, double, std::string, SymbolPtr, ObjectPtr>;

  Value() : data_(Undefined{}) {}
  Value(Undefined v) : data_(v) {}
  Value(Null v) : data_(v) {}
  Value(bool v) : data_(v) {}
  Value(double v) : data_(v) {}
  Value(int v) : data_(static_cast<double>(v)) {}
  Value(std::string v) : data_(std::move(v)) {}
  Value(const char *v) : data_(std::string(v ? v : "")) {}
  Value(SymbolPtr v) : data_(std::move(v)) {}
  Value(ObjectPtr v) : data_(std::move(v)) {}

  static Value undefined() { return Undefined{}; }
  static Value null() { return Null{}; }

  bool isUndefined() const noexcept { return std::holds_alternative<Undefined>(data_); }
  bool isNull() const noexcept { return std::holds_alternative<Null>(data_); }
  bool isBoolean() const noexcept { return std::holds_alternative<bool>(data_); }
  bool isNumber() const noexcept { return std::holds_alternative<double>(data_); }
  bool isString() const noexcept { return std::holds_alternative<std::string>(data_); }
  bool isSymbol() const noexcept { return std::holds_alternative<SymbolPtr>(data_); }
  bool isObject() const noexcept { return std::holds_alternative<ObjectPtr>(data_); }
  bool isNullish() const noexcept { return isUndefined() || isNull(); }

  bool asBoolean() const { return std::get<bool>(data_); }
  double asNumber() const { return std::get<double>(data_); }
  const std::string &asString() const { return std::get<std::string>(data_); }
  SymbolPtr asSymbol() const { return std::get<SymbolPtr>(data_); }
  ObjectPtr asObject() const { return std::get<ObjectPtr>(data_); }
  const Storage &storage() const noexcept { return data_; }

private:
  Storage data_;
};

enum class ObjectKind : std::uint8_t { Ordinary, Array, Function };
enum class DescriptorKind : std::uint8_t { Data, Accessor };

using Getter = std::function<Value(const Value &)>;
using Setter = std::function<void(const Value &, const Value &)>;

struct PropertyDescriptor {
  DescriptorKind kind = DescriptorKind::Data;
  Value value = Value::undefined();
  bool writable = false;
  bool enumerable = false;
  bool configurable = false;
  std::shared_ptr<Getter> getter;
  std::shared_ptr<Setter> setter;

  static PropertyDescriptor data(Value value, bool writable = false, bool enumerable = false,
                                 bool configurable = false) {
    PropertyDescriptor d;
    d.kind = DescriptorKind::Data;
    d.value = std::move(value);
    d.writable = writable;
    d.enumerable = enumerable;
    d.configurable = configurable;
    return d;
  }

  static PropertyDescriptor accessor(Getter getter = {}, Setter setter = {},
                                     bool enumerable = false, bool configurable = false) {
    PropertyDescriptor d;
    d.kind = DescriptorKind::Accessor;
    if (getter) d.getter = std::make_shared<Getter>(std::move(getter));
    if (setter) d.setter = std::make_shared<Setter>(std::move(setter));
    d.enumerable = enumerable;
    d.configurable = configurable;
    return d;
  }
};

struct PropertyKey {
  std::variant<std::string, SymbolPtr> value;

  PropertyKey(std::string key) : value(std::move(key)) {}
  PropertyKey(const char *key) : value(std::string(key ? key : "")) {}
  PropertyKey(SymbolPtr key) : value(std::move(key)) {}

  bool isString() const noexcept { return std::holds_alternative<std::string>(value); }
  const std::string &asString() const { return std::get<std::string>(value); }
  SymbolPtr asSymbol() const { return std::get<SymbolPtr>(value); }
};

struct Object {
  explicit Object(ObjectKind objectKind = ObjectKind::Ordinary) : kind(objectKind) {}
  virtual ~Object() = default;

  ObjectKind kind;
  ObjectPtr prototype;
  std::vector<std::pair<PropertyKey, PropertyDescriptor>> properties;
  bool extensible = true;
};

struct ArrayObject final : Object {
  ArrayObject() : Object(ObjectKind::Array) {}
  std::vector<Value> elements;
};

} // namespace edon::ecma
