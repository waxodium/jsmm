#include <rma/engine.hpp>

#include <stdexcept>
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

} // namespace rma
