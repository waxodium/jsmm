#include "compiler/ast_printer.hh"
#include "compiler/compiler.hh"
#include "compiler/parser.hh"
#include "engine/lexer.hh"
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
  std::cerr << "Usage: " << program << " <input.js> [-o <output>]\n  "
            << program << " --ast <input.js>\n";
}

std::string readFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open source file: " + path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} 

int main(int argc, char *argv[]) {
  if (argc == 3 && std::string(argv[1]) == "--ast") {
    try {
      const std::string source = readFile(argv[2]);
      rma::syntax::Lexer lexer(source);
      auto tokens = lexer.tokenize();
      if (lexer.failed()) {
        const auto &e = lexer.error();
        throw std::runtime_error("lexer error at " + std::to_string(e.position.line) + ":" +
                                 std::to_string(e.position.column) + ": " + e.message);
      }
      rma::compiler::Parser parser(std::move(tokens));
      auto program = parser.parse();
      rma::compiler::AstPrinter{}.print(program, std::cout);
      return 0;
    } catch (const std::exception &error) {
      std::cerr << "rma: " << error.what() << '\n';
      return 1;
    }
  }

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
