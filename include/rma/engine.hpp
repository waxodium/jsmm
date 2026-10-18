
#pragma once

#include <rma/error.hpp>
#include <rma/result.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rma {

using Executable = std::vector<std::uint8_t>;

struct Diagnostic final {
  Error status = Error::None;
  std::string message;
  std::size_t line = 0;
  std::size_t column = 0;
};

class DiagnosticBuffer final {
public:
  void add(Error status, std::string_view message, std::size_t line = 0, std::size_t column = 0);

  void clear() noexcept;

  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const Diagnostic &at(std::size_t index) const;

  [[nodiscard]] const std::vector<Diagnostic> &messages() const noexcept;
  [[nodiscard]] Error status() const noexcept;

private:
  std::vector<Diagnostic> messages_;
};

class Engine final {
public:
  [[nodiscard]] Result<Executable, Error> compile(std::string_view source,
                                                  DiagnosticBuffer *diagnostics = nullptr) const;
};

} // namespace rma
