#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace rma {

struct SourceLocation {
  std::size_t offset = 0;
  std::size_t line = 1;
  std::size_t column = 1;
};

struct SourceSpan {
  SourceLocation begin;
  SourceLocation end;
};

enum class DiagnosticCode {
  UnexpectedCharacter,
  UnterminatedString,
  InvalidEscapeSequence,
  UnexpectedToken,
  ExpectedExpression,
  UndefinedIdentifier,
  DuplicateBinding,
  InvalidOperand,
  CodeGenerationFailure
};

enum class Severity { Error, Warning };

struct Diagnostic {
  DiagnosticCode code = DiagnosticCode::UnexpectedToken;
  Severity severity = Severity::Error;
  SourceSpan span;
  std::string message;
};

class DiagnosticBuffer final {
public:
  void add(Diagnostic diagnostic) { messages_.push_back(std::move(diagnostic)); }

  void clear() noexcept { messages_.clear(); }

  [[nodiscard]] bool empty() const noexcept { return messages_.empty(); }

  [[nodiscard]] bool hasErrors() const noexcept {
    for (const auto &diagnostic : messages_) {
      if (diagnostic.severity == Severity::Error) return true;
    }
    return false;
  }

  [[nodiscard]] std::size_t size() const noexcept { return messages_.size(); }

  [[nodiscard]] const Diagnostic &at(std::size_t index) const { return messages_.at(index); }

  [[nodiscard]] const std::vector<Diagnostic> &messages() const noexcept { return messages_; }

private:
  std::vector<Diagnostic> messages_;
};

} // namespace rma
