#include "io/platform.hh"

#include <cstdio>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace rma::io {

void writeFile(const std::string &path, std::string_view content) {
  std::FILE *file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) throw std::runtime_error("cannot open file for writing: " + path);
  if (!content.empty() &&
      std::fwrite(content.data(), 1, content.size(), file) != content.size()) {
    std::fclose(file);
    throw std::runtime_error("cannot write file: " + path);
  }
  if (std::fclose(file) != 0) throw std::runtime_error("cannot write file: " + path);
}

void removeFile(const std::string &path) { (void)::unlink(path.c_str()); }

int runCommand(const std::vector<std::string> &command) {
  if (command.empty()) throw std::runtime_error("empty command");
  std::vector<char *> arguments;
  arguments.reserve(command.size() + 1);
  for (const auto &argument : command) arguments.push_back(const_cast<char *>(argument.c_str()));
  arguments.push_back(nullptr);

  const pid_t child = ::fork();
  if (child < 0) throw std::runtime_error("cannot fork: " + command.front());
  if (child == 0) {
    ::execvp(arguments.front(), arguments.data());
    std::perror("rma: execvp");
    ::_exit(127);
  }

  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
  }
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  throw std::runtime_error("command terminated abnormally: " + command.front());
}

}
