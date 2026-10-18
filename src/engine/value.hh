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

inline std::u16string utf8ToUtf16(const std::string &input) {
  std::u16string output;
  for (std::size_t i = 0; i < input.size();) {
    const auto lead = static_cast<unsigned char>(input[i]);
    std::uint32_t cp = 0;
    std::size_t count = 0;
    if (lead < 0x80) {
      cp = lead;
      count = 1;
    } else if ((lead & 0xE0) == 0xC0) {
      cp = lead & 0x1F;
      count = 2;
    } else if ((lead & 0xF0) == 0xE0) {
      cp = lead & 0x0F;
      count = 3;
    } else if ((lead & 0xF8) == 0xF0) {
      cp = lead & 0x07;
      count = 4;
    } else {
      output.push_back(u'\uFFFD');
      ++i;
      continue;
    }
    if (i + count > input.size()) {
      output.push_back(u'\uFFFD');
      ++i;
      continue;
    }
    bool valid = true;
    for (std::size_t j = 1; j < count; ++j) {
      const auto byte = static_cast<unsigned char>(input[i + j]);
      if ((byte & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      cp = (cp << 6) | (byte & 0x3F);
    }
    const std::uint32_t minimum = count == 1 ? 0 : count == 2 ? 0x80 : count == 3 ? 0x800 : 0x10000;
    if (!valid || cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
      output.push_back(u'\uFFFD');
      ++i;
      continue;
    }
    i += count;
    if (cp <= 0xFFFF) output.push_back(static_cast<char16_t>(cp));
    else {
      cp -= 0x10000;
      output.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      output.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return output;
}

inline std::string utf16ToUtf8(const std::u16string &input) {
  std::string output;
  for (std::size_t i = 0; i < input.size(); ++i) {
    std::uint32_t cp = input[i];
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      if (i + 1 < input.size() && input[i + 1] >= 0xDC00 && input[i + 1] <= 0xDFFF) {
        cp = 0x10000 + ((cp - 0xD800) << 10) + (input[++i] - 0xDC00);
      } else
        cp = 0xFFFD;
    } else if (cp >= 0xDC00 && cp <= 0xDFFF)
      cp = 0xFFFD;
    if (cp < 0x80) output.push_back(static_cast<char>(cp));
    else if (cp < 0x800) {
      output.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      output.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      output.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      output.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      output.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return output;
}

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
  using Storage = std::variant<Undefined, Null, bool, double, std::u16string, SymbolPtr, ObjectPtr>;

  Value() : data_(Undefined{}) {}
  Value(Undefined v) : data_(v) {}
  Value(Null v) : data_(v) {}
  Value(bool v) : data_(v) {}
  Value(double v) : data_(v) {}
  Value(int v) : data_(static_cast<double>(v)) {}
  Value(std::u16string v) : data_(std::move(v)) {}
  Value(std::string v) : data_(utf8ToUtf16(v)) {}
  Value(const char *v) : data_(utf8ToUtf16(std::string(v ? v : ""))) {}
  Value(SymbolPtr v) : data_(std::move(v)) {}
  Value(ObjectPtr v) : data_(std::move(v)) {}

  static Value undefined() { return Undefined{}; }
  static Value null() { return Null{}; }

  bool isUndefined() const noexcept { return std::holds_alternative<Undefined>(data_); }
  bool isNull() const noexcept { return std::holds_alternative<Null>(data_); }
  bool isBoolean() const noexcept { return std::holds_alternative<bool>(data_); }
  bool isNumber() const noexcept { return std::holds_alternative<double>(data_); }
  bool isString() const noexcept { return std::holds_alternative<std::u16string>(data_); }
  bool isSymbol() const noexcept { return std::holds_alternative<SymbolPtr>(data_); }
  bool isObject() const noexcept { return std::holds_alternative<ObjectPtr>(data_); }
  bool isNullish() const noexcept { return isUndefined() || isNull(); }

  bool asBoolean() const { return std::get<bool>(data_); }
  double asNumber() const { return std::get<double>(data_); }
  const std::u16string &asString() const { return std::get<std::u16string>(data_); }
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
