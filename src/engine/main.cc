#include "compiler/compiler.hh"

#include <exception>
#include <iostream>
#include <string>

namespace {
void usage(const char *program) {
  std::cerr << "Usage: " << program << " <input.js> -o <output>\n";
}
}

int main(int argc, char **argv) {
  if (argc != 4 || std::string(argv[2]) != "-o") {
    usage(argv[0]);
    return 2;
  }
  try {
    rma::compiler::Compiler{}.compileFile(argv[1], argv[3]);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "rma: error: " << error.what() << '\n';
    return 1;
  }
}
