#include "compiler/compiler.hh"
#include "platform.hh"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

void usage(const char *program) {
  std::cerr << "Usage: " << program << " <input.js> [-o <output>]\n";
}
} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    usage(argv[0]);
    return 2;
  }

  std::string inputPath;
  std::string outputPath;

  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];

    if (argument == "-o" || argument == "--output") {
      if (++i >= argc) {
        usage(argv[0]);
        return 2;
      }

      outputPath = argv[i];
    } else if (!argument.empty() && argument[0] == '-') {
      usage(argv[0]);
      return 2;
    } else if (inputPath.empty()) {
      inputPath = argument;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  if (inputPath.empty()) {
    usage(argv[0]);
    return 2;
  }

  if (outputPath.empty()) {
    outputPath = inputPath;
    const std::size_t extension = outputPath.rfind(".js");

    if (extension != std::string::npos && extension + 3 == outputPath.size()) {
      outputPath.replace(extension, 3, "");
    } else {
      outputPath += ".out";
    }
  }

  try {
    const std::string source = rma::cli::readFile(inputPath);

    const auto executable = rma::compiler::compileSource(source);

    rma::cli::writeExecutable(outputPath, executable);
  } catch (const std::exception &error) {
    std::cerr << "rma: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
