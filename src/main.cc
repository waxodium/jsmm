#include "builtins/builtin.hh"
#include "emit/codegen.hh"
#include "errors/errors.hh"
#include "io/platform.hh"
#include "reader/lexer.hh"
#include "reader/parser.hh"
#include "types/checker.hh"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void usage(const char *program) {
  std::cerr << "Usage: " << program << " <input.js> [-o <output>]\n";
}

std::string readFile(const std::string &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw jsmm::errors::ErrorException{
        jsmm::errors::ErrorStatus{jsmm::errors::ErrorCode::IO_CANNOT_OPEN_SOURCE_FILE, path, {}}};
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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
    jsmm::builtins::BuiltinRegistry::registerAll();

    const std::string source = readFile(inputPath);

    jsmm::reader::Lexer lexer(source);
    std::vector<jsmm::reader::Token> tokens = lexer.tokenize();
    if (lexer.failed())
      throw jsmm::errors::ErrorException{
          jsmm::errors::ErrorStatus{jsmm::errors::ErrorCode::CLI_LEX_ERROR, lexer.error().message,
                                   lexer.error().position}};

    jsmm::reader::Parser parser(std::move(tokens));
    jsmm::reader::Program program = parser.parse();

    jsmm::types::Checker checker;
    const jsmm::types::Annotations annotations = checker.check(program);

    jsmm::emit::CodeGenerator codegen;
    const std::string assembly = codegen.generate(program, annotations, checker.context());

    const std::string assemblyPath = outputPath + ".s";
    const std::string objectPath = outputPath + ".o";

    jsmm::io::writeFile(assemblyPath, assembly);

    const std::vector<std::string> assembler = {"as", "--64", "-o", objectPath, assemblyPath};
    if (const int status = jsmm::io::runCommand(assembler); status != 0)
      throw jsmm::errors::ErrorException{jsmm::errors::ErrorStatus{
          jsmm::errors::ErrorCode::CLI_ASSEMBLER_FAILED,
          "assembler failed with status " + std::to_string(status)}};

    const std::vector<std::string> linker = {"ld", "-o", outputPath, objectPath};
    if (const int status = jsmm::io::runCommand(linker); status != 0)
      throw jsmm::errors::ErrorException{jsmm::errors::ErrorStatus{
          jsmm::errors::ErrorCode::CLI_LINKER_FAILED,
          "linker failed with status " + std::to_string(status)}};

    jsmm::io::removeFile(assemblyPath);
    jsmm::io::removeFile(objectPath);
  } catch (const jsmm::errors::ErrorException &error) {
    const auto &status = error.status();
    std::cerr << jsmm::errors::formatError(status) << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "jsmm: " << error.what() << '\n';
    return 1;
  }

  return 0;
}
