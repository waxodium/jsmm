#pragma once

#include <cassert>
#include <type_traits>
#include <utility>
#include <variant>

namespace rma {

template <typename T, typename E> class [[nodiscard]] Result final {
  static_assert(!std::is_reference_v<T>, "Result cannot contain a reference type");
  static_assert(!std::is_reference_v<E>, "Result cannot contain an error reference");
  static_assert(!std::is_same_v<T, E>, "Result value and error types must differ");

public:
  using ValueType = T;
  using ErrorType = E;

  [[nodiscard]] static Result success(T value) {
    return Result(std::in_place_index<0>, std::move(value));
  }

  [[nodiscard]] static Result failure(E error) {
    return Result(std::in_place_index<1>, std::move(error));
  }

  [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }

  [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

  [[nodiscard]] T &value() & {
    assert(hasValue());
    return std::get<0>(storage_);
  }

  [[nodiscard]] const T &value() const & {
    assert(hasValue());
    return std::get<0>(storage_);
  }

  [[nodiscard]] T &&value() && {
    assert(hasValue());
    return std::get<0>(std::move(storage_));
  }

  [[nodiscard]] E &error() & {
    assert(!hasValue());
    return std::get<1>(storage_);
  }

  [[nodiscard]] const E &error() const & {
    assert(!hasValue());
    return std::get<1>(storage_);
  }

private:
  template <typename... Args>
  explicit Result(std::in_place_index_t<0> tag, Args &&...args)
      : storage_(tag, std::forward<Args>(args)...) {}

  template <typename... Args>
  explicit Result(std::in_place_index_t<1> tag, Args &&...args)
      : storage_(tag, std::forward<Args>(args)...) {}

  std::variant<T, E> storage_;
};

template <typename E> class [[nodiscard]] Result<void, E> final {
public:
  using ValueType = void;
  using ErrorType = E;

  [[nodiscard]] static Result success() { return Result(std::in_place_index<0>); }

  [[nodiscard]] static Result failure(E error) {
    return Result(std::in_place_index<1>, std::move(error));
  }

  [[nodiscard]] bool hasValue() const noexcept { return storage_.index() == 0; }

  [[nodiscard]] explicit operator bool() const noexcept { return hasValue(); }

  [[nodiscard]] E &error() & {
    assert(!hasValue());
    return std::get<1>(storage_);
  }

  [[nodiscard]] const E &error() const & {
    assert(!hasValue());
    return std::get<1>(storage_);
  }

private:
  explicit Result(std::in_place_index_t<0> tag) : storage_(tag) {}

  explicit Result(std::in_place_index_t<1> tag, E error) : storage_(tag, std::move(error)) {}

  std::variant<std::monostate, E> storage_;
};

} // namespace rma
