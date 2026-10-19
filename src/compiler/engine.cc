#include <rma/engine.hpp>

#include "compiler.hh"

#include <stdexcept>
#include <string>
#include <utility>

namespace rma {

void DiagnosticBuffer::add(Error status, std::string_view message, std::size_t line,
                           std::size_t column) {
  messages_.push_back({status, std::string(message), line, column});
}

void DiagnosticBuffer::clear() noexcept { messages_.clear(); }

bool DiagnosticBuffer::empty() const noexcept { return messages_.empty(); }

std::size_t DiagnosticBuffer::size() const noexcept { return messages_.size(); }

const Diagnostic &DiagnosticBuffer::at(std::size_t index) const { return messages_.at(index); }

const std::vector<Diagnostic> &DiagnosticBuffer::messages() const noexcept { return messages_; }

Error DiagnosticBuffer::status() const noexcept {
  if (messages_.empty()) return Error::None;

  return messages_.front().status;
}

Result<Executable, Error> Engine::compile(std::string_view source,
                                          DiagnosticBuffer *diagnostics) const {
  DiagnosticBuffer local;
  DiagnosticBuffer *out = diagnostics ? diagnostics : &local;

  try {
    std::vector<std::uint8_t> bytes = compiler::compileSource(source);
    return Result<Executable, Error>::success(std::move(bytes));
  } catch (const std::runtime_error &error) {
    out->add(Error::Internal, error.what());
    return Result<Executable, Error>::failure(Error::Internal);
  } catch (const std::exception &error) {
    out->add(Error::Internal, error.what());
    return Result<Executable, Error>::failure(Error::Internal);
  } catch (...) {
    out->add(Error::Internal, "unknown error during compilation");
    return Result<Executable, Error>::failure(Error::Internal);
  }
}

} // namespace rma
