#include "io/platform.hh"

#include "errors/errors.hh"

#include <cstdio>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace jsmm::io {

void writeFile(const std::string &path, std::string_view content) {
  std::FILE *file = std::fopen(path.c_str(), "wb");
  if (file == nullptr)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::IO_CANNOT_OPEN_FILE_FOR_WRITING, path}};
  if (!content.empty() && std::fwrite(content.data(), 1, content.size(), file) != content.size()) {
    std::fclose(file);
    throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::IO_CANNOT_WRITE_FILE, path}};
  }
  if (std::fclose(file) != 0)
    throw errors::ErrorException{errors::ErrorStatus{errors::ErrorCode::IO_CANNOT_WRITE_FILE, path}};
}

void removeFile(const std::string &path) { (void)::unlink(path.c_str()); }

int runCommand(const std::vector<std::string> &command) {
  if (command.empty())
    throw errors::ErrorException{
        errors::ErrorStatus{errors::ErrorCode::IO_EMPTY_COMMAND, {}, {}}};
  std::vector<char *> arguments;
  arguments.reserve(command.size() + 1);
  for (const auto &argument : command) arguments.push_back(const_cast<char *>(argument.c_str()));
  arguments.push_back(nullptr);

  const pid_t child = ::fork();
  if (child < 0)
    throw errors::ErrorException{errors::ErrorStatus{
        errors::ErrorCode::IO_CANNOT_FORK, command.front()}};
  if (child == 0) {
    ::execvp(arguments.front(), arguments.data());
    std::perror("jsmm: execvp");
    ::_exit(127);
  }

  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {}
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  throw errors::ErrorException{errors::ErrorStatus{
      errors::ErrorCode::IO_COMMAND_TERMINATED_ABNORMALLY, command.front()}};
}

} // namespace jsmm::io
