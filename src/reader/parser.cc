#include "reader/parser.hh"

#include "errors/errors.hh"

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace jsmm::reader {
namespace {

std::size_t skipSizedIntArguments(const std::vector<Token> &tokens, std::size_t cursor) {
  if (cursor + 4 >= tokens.size()) return cursor;
  if (tokens[cursor].lexeme != "<") return cursor;
  if (tokens[cursor + 1].kind != TokenKind::NumberLiteral) return cursor;
  if (tokens[cursor + 2].lexeme != ",") return cursor;
  if (tokens[cursor + 3].lexeme != "signed" && tokens[cursor + 3].lexeme != "unsigned")
    return cursor;
  if (tokens[cursor + 4].lexeme != ">") return cursor;
  return cursor + 5;
}

bool decodeBitsLiteral(const std::string &spelling, std::string &out) {
  if (spelling.size() <= 2) return false;
  std::string bits = spelling.substr(2);
  for (const char bit : bits)
    if (bit != '0' && bit != '1') return false;
  while (bits.size() % 8 != 0) bits.insert(bits.begin(), '0');
  for (std::size_t index = 0; index < bits.size(); index += 8) {
    unsigned value = 0;
    for (std::size_t bit = 0; bit < 8; ++bit)
      value = value * 2U + static_cast<unsigned>(bits[index + bit] - '0');
    out.push_back(static_cast<char>(value));
  }
  return true;
}

} // namespace

const std::unordered_map<std::string, Parser::InfixParselet> &Parser::infixParselets() {
  using P = Parser;
  static const std::unordered_map<std::string, P::InfixParselet> table{
      {"=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"+=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"-=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"*=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"/=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"%=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"&=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"|=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"^=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"<<=", {{1, true}, &P::parseAssignmentInfix, true}},
      {">>=", {{1, true}, &P::parseAssignmentInfix, true}},
      {">>>=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"||", {{2, false}, &P::parseBinaryInfix}},
      {"&&", {{3, false}, &P::parseBinaryInfix}},
      {"|", {{4, false}, &P::parseBinaryInfix}},
      {"^", {{5, false}, &P::parseBinaryInfix}},
      {"&", {{6, false}, &P::parseBinaryInfix}},
      {"==", {{7, false}, &P::parseBinaryInfix}},
      {"!=", {{7, false}, &P::parseBinaryInfix}},
      {"<", {{8, false}, &P::parseBinaryInfix}},
      {">", {{8, false}, &P::parseBinaryInfix}},
      {"<=", {{8, false}, &P::parseBinaryInfix}},
      {">=", {{8, false}, &P::parseBinaryInfix}},
      {"<<", {{9, false}, &P::parseBinaryInfix}},
      {">>", {{9, false}, &P::parseBinaryInfix}},
      {">>>", {{9, false}, &P::parseBinaryInfix}},
      {"+", {{10, false}, &P::parseBinaryInfix}},
      {"-", {{10, false}, &P::parseBinaryInfix}},
      {"*", {{11, false}, &P::parseBinaryInfix}},
      {"/", {{11, false}, &P::parseBinaryInfix}},
      {"%", {{11, false}, &P::parseBinaryInfix}}};
  return table;
}

const std::unordered_map<std::string, Parser::PrefixParser> &Parser::prefixParselets() {
  using P = Parser;
  static const std::unordered_map<std::string, P::PrefixParser> table{
      {"!", &P::parseUnaryPrefix},
      {"~", &P::parseUnaryPrefix},
      {"+", &P::parseUnaryPrefix},
      {"-", &P::parseUnaryPrefix},
      {"&", &P::parseUnaryPrefix},
      {"*", &P::parseUnaryPrefix},
      {"typeof", &P::parseUnaryPrefix},
      {"void", &P::parseUnaryPrefix}};
  return table;
}

const std::unordered_map<std::string, Parser::PostfixParser> &Parser::postfixParselets() {
  using P = Parser;
  static const std::unordered_map<std::string, P::PostfixParser> table{
      {"(", &P::parseCallPostfix},
      {".", &P::parseMemberPostfix},
      {"[", &P::parseComputedMemberPostfix}};
  return table;
}

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

const Token &Parser::current() const { return tokens_.at(index_); }

bool Parser::eat(std::string_view lexeme) {
  if (current().lexeme != lexeme) return false;
  ++index_;
  return true;
}

const Token &Parser::consume() {
  const Token &token = current();
  if (token.kind == TokenKind::End)
    fail("unexpected end of input", errors::ErrorCode::PARSE_UNEXPECTED_END_OF_INPUT);
  ++index_;
  return token;
}

[[noreturn]] void Parser::fail(const std::string &message) const {
  fail(message, errors::ErrorCode::PARSE_EXPECTED_EXPRESSION);
}

[[noreturn]] void Parser::fail(const std::string &message, errors::ErrorCode code) const {
  throw errors::ErrorException{errors::ErrorStatus{code, message, current().start}};
}

void Parser::optionalSemicolon() { (void)eat(";"); }

void Parser::ensureSemicolonOrASI() {
  if (eat(";")) return;
  if (current().kind == TokenKind::End) return;
  if (current().lexeme == "}") return;
  if (current().start.line > tokens_.at(index_ > 0 ? index_ - 1 : 0).end.line) return;
  if (current().kind == TokenKind::Keyword ||
      current().kind == TokenKind::Identifier ||
      current().lexeme == "{") {
    fail("missing semicolon before statement", errors::ErrorCode::PARSE_MISSING_SEMICOLON);
  }
}

Program Parser::parse() {
  Program program;
  while (current().kind != TokenKind::End) program.statements.push_back(std::make_unique<Statement>(parseStatement()));
  return program;
}

Statement Parser::parseStatement() {
  const SourcePosition start = current().start;
  Statement statement = [&]() {
    if (startsFunctionDeclaration()) return parseFunctionDeclaration();
    if (current().kind == TokenKind::Keyword) {
      const std::string &keyword = current().lexeme;
      if (keyword == "const" || keyword == "string" || keyword == "bool" ||
          keyword == "boolean" || keyword == "bytes" || keyword == "auto" ||
          keyword == "void" || keyword == "int")
        return parseDeclaration();
      if (keyword == "type") return parseTypeDeclaration();
      if (keyword == "return") return parseReturnStatement();
      if (keyword == "if") return parseIfStatement();
      if (keyword == "while") return parseWhileStatement();
      if (keyword == "for") return parseForStatement();
      if (keyword == "switch") return parseSwitchStatement();
      if (keyword == "break") return parseBreakStatement();
      if (keyword == "continue") return parseContinueStatement();
    }
    if (current().lexeme == "{") return parseBlock();
    if (current().lexeme == ";") return parseEmptyStatement();
    if (current().kind == TokenKind::Identifier && peekNextIsColon()) {
      return parseLabeledStatement();
    }
    if (startsVariableDeclaration()) return parseDeclaration();
    return parseExpressionStatement();
  }();
  statement.range = SourceRange{start, tokens_.at(index_ == 0 ? 0 : index_ - 1).end};
  return statement;
}

Statement Parser::parseEmptyStatement() {
  const SourcePosition start = current().start;
  consume();
  return Statement(ExpressionStatement{nullptr}, SourceRange{start, tokens_.at(index_ - 1).end});
}

Statement Parser::parseExpressionStatement() {
  auto expression = parseExpression();
  ensureSemicolonOrASI();
  return Statement(ExpressionStatement{std::move(expression)}, SourceRange{});
}

Statement Parser::parseDeclaration() {
  const std::string kind = consume().lexeme;
  const bool isQualifier = kind == "const";
  const auto isBuiltinType = [](const std::string &text) {
    return text == "string" || text == "bool" || text == "boolean" || text == "byte" ||
           text == "bytes" || text == "auto" || text == "void" || text == "null" ||
           text == "int";
  };

  std::shared_ptr<TypeExpr> annotation = std::make_shared<TypeExpr>();

  if (!isQualifier) {
    // Direct type declaration: number x = 42;
    annotation->names.push_back(kind);
    annotation->pointerDepth.push_back(parsePointerStars());
    parseIntTypeArguments(annotation);
    annotation->pointerDepth.back() += parsePointerStars();
    parseArraySize(annotation);
    while (eat("|") || eat(",")) {
      if (current().kind != TokenKind::Identifier && current().kind != TokenKind::Keyword)
        fail("expected a type name", errors::ErrorCode::PARSE_EXPECTED_TYPE_NAME);
      annotation->names.push_back(consume().lexeme);
      annotation->pointerDepth.push_back(parsePointerStars());
    }
  }

  VariableDeclaration declaration{kind, {}};
  do {
    std::shared_ptr<TypeExpr> declaratorAnnotation = annotation;
    if (isQualifier) {
      const bool typeish =
          current().kind == TokenKind::Identifier || current().kind == TokenKind::Keyword;
      bool isName = false;
      if (typeish && index_ + 1 < tokens_.size()) {
        const auto &nextTok = tokens_[index_ + 1];
        if (nextTok.lexeme == "=" || nextTok.lexeme == ";" || nextTok.lexeme == ":" ||
            (nextTok.lexeme == "," && current().kind == TokenKind::Identifier))
          isName = true;
      }
      if (typeish && !isName) {
        const bool builtin = isBuiltinType(current().lexeme);
        auto prefix = std::make_shared<TypeExpr>();
        prefix->names.push_back(consume().lexeme);
        prefix->pointerDepth.push_back(parsePointerStars());
        parseIntTypeArguments(prefix);
        prefix->pointerDepth.back() += parsePointerStars();
        parseArraySize(prefix);
        for (;;) {
          if (eat("|")) {
            if (current().kind != TokenKind::Identifier && current().kind != TokenKind::Keyword)
              fail("expected a type name", errors::ErrorCode::PARSE_EXPECTED_TYPE_NAME);
            prefix->names.push_back(consume().lexeme);
            prefix->pointerDepth.push_back(parsePointerStars());
            continue;
          }
          if (builtin && current().lexeme == "," && index_ + 2 < tokens_.size() &&
              isBuiltinType(tokens_[index_ + 1].lexeme) &&
              (tokens_[index_ + 2].kind == TokenKind::Identifier ||
               tokens_[index_ + 2].kind == TokenKind::Keyword)) {
            consume();
            prefix->names.push_back(consume().lexeme);
            prefix->pointerDepth.push_back(parsePointerStars());
            continue;
          }
          break;
        }
        annotation = prefix;
        declaratorAnnotation = std::move(prefix);
      }
    }

    if (current().kind != TokenKind::Identifier)
      fail("expected binding name", errors::ErrorCode::PARSE_EXPECTED_BINDING_NAME);
    const std::string name = consume().lexeme;
    if (eat("[")) {
      if (current().kind != TokenKind::NumberLiteral)
        fail("expected array size", errors::ErrorCode::PARSE_EXPECTED_EXPRESSION);
      declaratorAnnotation = std::make_shared<TypeExpr>(*declaratorAnnotation);
      declaratorAnnotation->arraySize = std::stoull(consume().lexeme);
      declaratorAnnotation->isArray = true;
      if (!eat("]"))
        fail("expected ']' after array size",
             errors::ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_ARRAY_LITERAL);
    }
    if (eat(":")) declaratorAnnotation = parseTypeAnnotation();
    ExprPtr initializer;
    if (eat("=")) {
      initializer = parseExpression();
    } else if (kind == "const") {
      fail("const declaration requires an initializer",
           errors::ErrorCode::PARSE_CONST_REQUIRES_INITIALIZER);
    }
    declaration.declarations.push_back(
        {name, std::move(initializer), std::move(declaratorAnnotation)});
  } while (eat(","));
  ensureSemicolonOrASI();
  return Statement(std::move(declaration));
}

Statement Parser::parseTypeDeclaration() {
  consume(); // consume 'type'
  if (current().kind != TokenKind::Identifier && current().kind != TokenKind::Keyword)
    fail("expected type alias name", errors::ErrorCode::PARSE_EXPECTED_BINDING_NAME);
  const std::string name = consume().lexeme;
  if (!eat("="))
    fail("expected '=' after type alias name",
         errors::ErrorCode::PARSE_EXPECTED_COLON_AFTER_OBJECT_PROPERTY_NAME);
  auto annotation = parseTypeAnnotation();
  ensureSemicolonOrASI();
  return Statement(VariableDeclaration{"type", {{name, nullptr, annotation}}, {}});
}

int Parser::parsePointerStars() {
  int depth = 0;
  for (;;) {
    if (eat("*")) {
      ++depth;
    } else if (eat("**")) {
      depth += 2;
    } else {
      return depth;
    }
  }
}

void Parser::parseArraySize(const std::shared_ptr<TypeExpr> &annotation) {
  if (!eat("[")) return;
  if (eat("]")) {
    annotation->isArray = true;
    annotation->arraySize = 0;  // unsized/slice array
    return;
  }
  if (current().kind != TokenKind::NumberLiteral)
    fail("expected array size", errors::ErrorCode::PARSE_EXPECTED_EXPRESSION);
  annotation->arraySize = std::stoull(consume().lexeme);
  annotation->isArray = true;
  if (!eat("]"))
    fail("expected ']' after array size",
         errors::ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_ARRAY_LITERAL);
}

std::shared_ptr<TypeExpr> Parser::parseTypeAnnotation() {
  auto annotation = std::make_shared<TypeExpr>();
  const SourcePosition start = current().start;

  if (eat("{")) {
    annotation->isStruct = true;
    annotation->pointerDepth.push_back(0);
    if (!eat("}")) {
      do {
        if (current().kind != TokenKind::Identifier && current().kind != TokenKind::Keyword)
          fail("expected struct field type name",
               errors::ErrorCode::PARSE_EXPECTED_TYPE_NAME);
        std::string fieldType = consume().lexeme;
        if (current().kind != TokenKind::Identifier)
          fail("expected struct field name",
               errors::ErrorCode::PARSE_EXPECTED_BINDING_NAME);
        std::string fieldName = consume().lexeme;
        auto fieldAnnotation = std::make_shared<TypeExpr>();
        fieldAnnotation->names.push_back(fieldType);
        fieldAnnotation->pointerDepth.push_back(parsePointerStars());
        parseIntTypeArguments(fieldAnnotation);
        fieldAnnotation->pointerDepth.back() += parsePointerStars();
        annotation->fields.push_back({fieldName, fieldAnnotation});
        if (!eat(";"))
          fail("expected ';' after struct field",
               errors::ErrorCode::PARSE_EXPECTED_COLON_AFTER_OBJECT_PROPERTY_NAME);
      } while (current().lexeme != "}");
      if (!eat("}"))
        fail("expected '}' after struct fields",
             errors::ErrorCode::PARSE_EXPECTED_RBRACE_AFTER_OBJECT_LITERAL);
    }
    parseIntTypeArguments(annotation);
    annotation->pointerDepth.back() += parsePointerStars();
    annotation->range = SourceRange{start, tokens_.at(index_ - 1).end};
    return annotation;
  }

  do {
    const TokenKind kind = current().kind;
    if (kind != TokenKind::Identifier && kind != TokenKind::Keyword)
      fail("expected a type name", errors::ErrorCode::PARSE_EXPECTED_TYPE_NAME);
    annotation->names.push_back(consume().lexeme);
    annotation->pointerDepth.push_back(parsePointerStars());
  } while (eat("|") || eat(","));

  parseIntTypeArguments(annotation);
  annotation->pointerDepth.back() += parsePointerStars();

  parseArraySize(annotation);

  annotation->range = SourceRange{start, tokens_.at(index_ - 1).end};
  return annotation;
}void Parser::parseIntTypeArguments(const std::shared_ptr<TypeExpr> &annotation) {
  if (annotation->names.size() != 1 || annotation->pointerDepth[0] != 0) return;
  if (!eat("<")) return;
  annotation->isIntType = true;
  if (current().kind != TokenKind::NumberLiteral)
    fail("expected integer size in sized int type", errors::ErrorCode::PARSE_EXPECTED_EXPRESSION);
  annotation->intSize = std::stoull(consume().lexeme);
  if (!eat(","))
    fail("expected ',' in sized int type",
         errors::ErrorCode::PARSE_EXPECTED_COLON_AFTER_OBJECT_PROPERTY_NAME);
  const std::string signedness = consume().lexeme;
  if (signedness == "signed") {
    annotation->intSigned = true;
  } else if (signedness == "unsigned") {
    annotation->intSigned = false;
  } else {
    fail("expected 'signed' or 'unsigned' in sized int type",
         errors::ErrorCode::PARSE_EXPECTED_EXPRESSION);
  }
  if (!eat(">"))
    fail("expected '>' after sized int type arguments",
         errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_PARAMETERS);
}

std::shared_ptr<TypeExpr> Parser::parseTypeOperandAnnotation() {
  auto annotation = std::make_shared<TypeExpr>();
  const SourcePosition start = current().start;
  if (current().kind != TokenKind::Identifier && current().kind != TokenKind::Keyword)
    fail("expected a type name", errors::ErrorCode::PARSE_EXPECTED_TYPE_NAME);
  annotation->names.push_back(consume().lexeme);
  annotation->pointerDepth.push_back(parsePointerStars());
  while (eat("|")) {
    if (current().kind != TokenKind::Identifier && current().kind != TokenKind::Keyword)
      fail("expected a type name", errors::ErrorCode::PARSE_EXPECTED_TYPE_NAME);
    annotation->names.push_back(consume().lexeme);
    annotation->pointerDepth.push_back(parsePointerStars());
  }
  parseIntTypeArguments(annotation);
  annotation->pointerDepth.back() += parsePointerStars();
  if (eat("[")) {
    if (current().kind != TokenKind::NumberLiteral)
      fail("expected array size", errors::ErrorCode::PARSE_EXPECTED_EXPRESSION);
    annotation->arraySize = std::stoull(consume().lexeme);
    annotation->isArray = true;
    if (!eat("]"))
      fail("expected ']' after array size",
           errors::ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_ARRAY_LITERAL);
  }
  annotation->range = SourceRange{start, tokens_.at(index_ - 1).end};
  return annotation;
}

Statement Parser::parseFunctionDeclaration() {
  const TokenKind firstKind = current().kind;
  if (firstKind != TokenKind::Identifier && firstKind != TokenKind::Keyword)
    fail("expected return type or function name", errors::ErrorCode::PARSE_EXPECTED_TYPE_NAME);
  std::shared_ptr<TypeExpr> returnAnnotation = parseTypeAnnotation();

  if (current().kind != TokenKind::Identifier)
    fail("expected function name", errors::ErrorCode::PARSE_EXPECTED_FUNCTION_NAME);
  FunctionDeclaration function{consume().lexeme, {}, nullptr};
  function.returnAnnotation = std::move(returnAnnotation);
  if (!eat("("))
    fail("expected '(' after function name",
         errors::ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_FUNCTION_NAME);
  if (!eat(")")) {
    do {
      std::shared_ptr<TypeExpr> parameterAnnotation;
      const bool firstIsTypeName = current().kind == TokenKind::Identifier ||
                                   current().kind == TokenKind::Keyword;
      if (firstIsTypeName && index_ + 1 < tokens_.size() &&
          (tokens_[index_ + 1].kind == TokenKind::Identifier ||
           tokens_[index_ + 1].kind == TokenKind::Keyword ||
           tokens_[index_ + 1].lexeme == "*" || tokens_[index_ + 1].lexeme == "**" ||
           tokens_[index_ + 1].lexeme == "(" || tokens_[index_ + 1].lexeme == "[" ||
           tokens_[index_ + 1].lexeme == "<" || tokens_[index_ + 1].lexeme == "...")) {
        parameterAnnotation = parseTypeAnnotation();
      }
      if (current().kind != TokenKind::Identifier && current().kind != TokenKind::Keyword
        && current().kind != TokenKind::Punctuator)
        fail("expected parameter name", errors::ErrorCode::PARSE_EXPECTED_PARAMETER_NAME);
      const Token parameter = consume();
      if (parameter.lexeme == "...") {
        function.parameters.push_back(
            {"...", SourceRange{parameter.start, parameter.end}, nullptr, true});
        if (eat(","))
          fail("a variadic parameter must be the last parameter",
               errors::ErrorCode::PARSE_REST_PARAMETER_MUST_BE_LAST);
        break;
      }
      function.parameters.push_back(
          {parameter.lexeme, SourceRange{parameter.start, parameter.end},
           std::move(parameterAnnotation), false});
    } while (eat(","));
    if (!eat(")"))
      fail("expected ')' after parameters",
           errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_PARAMETERS);
  }
  if (!eat("{"))
    fail("expected '{' before function body",
         errors::ErrorCode::PARSE_EXPECTED_LBRACE_BEFORE_FUNCTION_BODY);
  auto body = std::make_unique<BlockStatement>();
  body->statements = parseBlockBody();
  function.body = std::move(body);
  ensureSemicolonOrASI();
  return Statement(std::move(function));
}

Statement Parser::parseReturnStatement() {
  const Token &keyword = consume();
  const std::size_t keywordLine = keyword.start.line;
  if (current().lexeme != ";" && current().lexeme != "}" && current().kind != TokenKind::End) {
    if (current().start.line > keywordLine) {
      return Statement(ReturnStatement{nullptr}, SourceRange{keyword.start, keyword.end});
    }
    ExprPtr argument = parseExpression();
    ensureSemicolonOrASI();
    return Statement(ReturnStatement{std::move(argument)}, SourceRange{keyword.start, keyword.end});
  }
  return Statement(ReturnStatement{nullptr}, SourceRange{keyword.start, keyword.end});
}

Statement Parser::parseIfStatement() {
  consume();
  if (!eat("(")) fail("expected '(' after 'if'", errors::ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_IF);
  auto condition = parseExpression();
  if (!eat(")"))
    fail("expected ')' after if condition",
         errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_IF_CONDITION);
  StatementList consequent = parseConsequent();
  StatementList alternate;
  if (eat("else")) {
    alternate = parseConsequent();
  }
  return Statement(IfStatement{std::move(condition), std::move(consequent), std::move(alternate)});
}

Statement Parser::parseWhileStatement() {
  consume();
  if (!eat("("))
    fail("expected '(' after 'while'", errors::ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_WHILE);
  auto condition = parseExpression();
  if (!eat(")"))
    fail("expected ')' after while condition",
         errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_WHILE_CONDITION);
  StatementList body = parseConsequent();
  return Statement(WhileStatement{std::move(condition), std::move(body)});
}

Statement Parser::parseForStatement() {
  consume();
  if (!eat("("))
    fail("expected '(' after 'for'", errors::ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_FOR);
  std::unique_ptr<VariableDeclaration> init;
  ExprPtr initExpression;
  bool initHasSemicolon = false;
  const bool declarationInit =
      current().lexeme == "const" || current().lexeme == "string" ||
      current().lexeme == "bool" || current().lexeme == "boolean" ||
      current().lexeme == "bytes" || current().lexeme == "auto" ||
      current().lexeme == "int" || startsVariableDeclaration();
  if (declarationInit) {
    Statement declaration = parseDeclaration();
    if (auto *variable = std::get_if<VariableDeclaration>(&declaration.node)) {
      init = std::make_unique<VariableDeclaration>(std::move(*variable));
      initHasSemicolon = true;
    }
  } else if (current().lexeme != ";") {
    initExpression = parseExpression();
  }
  if (!initHasSemicolon && !eat(";"))
    fail("expected ';' after for-init", errors::ErrorCode::PARSE_EXPECTED_SEMICOLON_AFTER_FOR_INIT);
  ExprPtr condition;
  if (current().lexeme != ";") condition = parseExpression();
  if (!eat(";"))
    fail("expected ';' after for-condition",
         errors::ErrorCode::PARSE_EXPECTED_SEMICOLON_AFTER_FOR_CONDITION);
  ExprPtr update;
  if (current().lexeme != ")") update = parseExpression();
  if (!eat(")"))
    fail("expected ')' after for clauses",
         errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_FOR_CLAUSES);
  StatementList body = parseConsequent();
  return Statement(ForStatement{std::move(init), std::move(initExpression), std::move(condition),
                                std::move(update), std::move(body)});
}

Statement Parser::parseSwitchStatement() {
  const SourcePosition start = consume().start;
  if (!eat("("))
    fail("expected '(' after 'switch'", errors::ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_IF);
  auto discriminant = parseExpression();
  if (!eat(")"))
    fail("expected ')' after switch expression",
         errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_IF_CONDITION);
  if (!eat("{"))
    fail("expected '{' before switch body",
         errors::ErrorCode::PARSE_EXPECTED_LBRACE_BEFORE_FUNCTION_BODY);
  SwitchStatement statement{std::move(discriminant), {}, {}};
  while (current().kind != TokenKind::End && current().lexeme != "}") {
    const SourcePosition caseStart = current().start;
    ExprPtr value;
    if (eat("case")) {
      value = parseExpression();
    } else if (!eat("default")) {
      fail("expected 'case' or 'default' in switch body");
    }
    if (!eat(":"))
      fail("expected ':' after switch case label",
           errors::ErrorCode::PARSE_EXPECTED_COLON_AFTER_OBJECT_PROPERTY_NAME);
    SwitchCase clause{std::move(value), {}, {}};
    while (current().kind != TokenKind::End && current().lexeme != "case" &&
           current().lexeme != "default" && current().lexeme != "}")
      clause.statements.push_back(std::make_unique<Statement>(parseStatement()));
    clause.range = SourceRange{caseStart, tokens_.at(index_ == 0 ? 0 : index_ - 1).end};
    statement.cases.push_back(std::move(clause));
  }
  if (!eat("}"))
    fail("expected '}' after switch body",
         errors::ErrorCode::PARSE_EXPECTED_RBRACE_AFTER_OBJECT_LITERAL);
  const SourceRange range{start, tokens_.at(index_ - 1).end};
  statement.range = range;
  return Statement(std::move(statement), range);
}

Statement Parser::parseBreakStatement() {
  const SourcePosition start = current().start;
  const Token &keyword = consume();
  const std::size_t keywordLine = keyword.start.line;
  if (current().lexeme != ";" && current().lexeme != "}" && current().kind != TokenKind::End) {
    if (current().start.line > keywordLine) {
      return Statement(BreakStatement{}, SourceRange{start, keyword.end});
    }
    if (current().kind == TokenKind::Identifier) {
      auto label = std::make_shared<Identifier>(Identifier{consume().lexeme, {}});
      return Statement(BreakStatement{label}, SourceRange{start, tokens_.at(index_ - 1).end});
    }
    fail("unexpected token after 'break'", errors::ErrorCode::PARSE_UNEXPECTED_TOKEN_AFTER_BREAK);
  }
  return Statement(BreakStatement{}, SourceRange{start, keyword.end});
}

Statement Parser::parseContinueStatement() {
  const SourcePosition start = current().start;
  const Token &keyword = consume();
  const std::size_t keywordLine = keyword.start.line;
  if (current().lexeme != ";" && current().lexeme != "}" && current().kind != TokenKind::End) {
    if (current().start.line > keywordLine) {
      return Statement(ContinueStatement{}, SourceRange{start, keyword.end});
    }
    if (current().kind == TokenKind::Identifier) {
      auto label = std::make_shared<Identifier>(Identifier{consume().lexeme, {}});
      return Statement(ContinueStatement{label}, SourceRange{start, tokens_.at(index_ - 1).end});
    }
    fail("unexpected token after 'continue'", errors::ErrorCode::PARSE_UNEXPECTED_TOKEN_AFTER_CONTINUE);
  }
  return Statement(ContinueStatement{}, SourceRange{start, keyword.end});
}

Statement Parser::parseBlock() {
  consume();
  auto body = std::make_unique<BlockStatement>();
  body->statements = parseBlockBody();
  return Statement(BlockStatement{std::move(body->statements)}, SourceRange{});
}

StatementList Parser::parseBlockBody() {
  StatementList statements;
  while (current().kind != TokenKind::End && current().lexeme != "}")
    statements.push_back(std::make_unique<Statement>(parseStatement()));
  if (!eat("}"))
    fail("expected '}' to close block", errors::ErrorCode::PARSE_EXPECTED_RBRACE_TO_CLOSE_BLOCK);
  return statements;
}

StatementList Parser::parseConsequent() {
  if (eat("{")) {
    return parseBlockBody();
  }
  StatementList list;
  list.push_back(std::make_unique<Statement>(parseStatement()));
  return list;
}

ExprPtr Parser::parseExpression(int minimumBindingPower) {
  auto left = parsePrefix();
  for (;;) {
    const auto found = infixParselets().find(current().lexeme);
    if (found == infixParselets().end() || found->second.info.bindingPower < minimumBindingPower)
      break;
    const auto parselet = found->second;
    const std::string op = consume().lexeme;
    left = (this->*parselet.parse)(std::move(left), op, parselet.info);
  }
  return left;
}

ExprPtr Parser::parsePrefix() {
  const auto found = prefixParselets().find(current().lexeme);
  if (found != prefixParselets().end()) return (this->*found->second)();
  return parsePostfix(parsePrimary());
}

ExprPtr Parser::parseUnaryPrefix() {
  const SourcePosition start = current().start;
  const std::string op = consume().lexeme;
  auto operand = parsePrefix();
  if (op == "&") {
    return std::make_shared<Expression>(AddressOfExpr{std::move(operand)},
                                    SourceRange{start, tokens_.at(index_ - 1).end});
  }
  if (op == "*" || op == "**") {
    const SourceRange range{start, tokens_.at(index_ - 1).end};
    auto dereference = std::make_shared<Expression>(DereferenceExpr{std::move(operand)}, range);
    if (op == "*")
      return dereference;
    return std::make_shared<Expression>(DereferenceExpr{std::move(dereference)}, range);
  }
  return std::make_shared<Expression>(UnaryExpr{op, std::move(operand)},
                                SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parseBinaryInfix(ExprPtr left, const std::string &op, OperatorInfo info) {
  const SourcePosition start = left->range.start;
  auto right = parseExpression(info.bindingPower + (info.rightAssociative ? 0 : 1));
  const SourceRange sourceRange{start, right->range.end};
  return std::make_shared<Expression>(BinaryExpr{op, std::move(left), std::move(right)}, sourceRange);
}

ExprPtr Parser::parseAssignmentInfix(ExprPtr left, const std::string &op, OperatorInfo info) {
  const SourcePosition start = left->range.start;
  auto right = parseExpression(info.bindingPower);
  const SourceRange sourceRange{start, right->range.end};
  return std::make_shared<Expression>(AssignmentExpr{op, std::move(left), std::move(right)}, sourceRange);
}

ExprPtr Parser::parsePostfix(ExprPtr expression) {
  for (;;) {
    const auto found = postfixParselets().find(current().lexeme);
    if (found == postfixParselets().end()) break;
    expression = (this->*found->second)(std::move(expression));
  }
  return expression;
}

ExprPtr Parser::parseCallPostfix(ExprPtr expression) {
  (void)consume();
  const SourcePosition start = expression->range.start;
  const auto *callee = std::get_if<IdentifierExpr>(&expression->node);
  const bool typeOperand =
      callee != nullptr && (callee->name == "sizeof" || callee->name == "alignof");
  std::vector<ExprPtr> arguments;
  if (!eat(")")) {
    do {
      if (typeOperand && arguments.empty()) {
        auto annotation = parseTypeOperandAnnotation();
        const SourceRange operandRange = annotation->range;
        arguments.push_back(
            std::make_shared<Expression>(TypeOperandExpr{std::move(annotation)}, operandRange));
      } else {
        arguments.push_back(parseExpression());
      }
    } while (eat(","));
    if (!eat(")"))
      fail("expected ')' after call arguments",
           errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_CALL_ARGUMENTS);
  }
  return std::make_shared<Expression>(CallExpr{std::move(expression), std::move(arguments)},
                                SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parseMemberPostfix(ExprPtr expression) {
  (void)consume();
  const SourcePosition start = expression->range.start;
  if (current().kind != TokenKind::Identifier && current().kind != TokenKind::Keyword &&
      current().kind != TokenKind::NumberLiteral)
    fail("expected property name after '.'",
         errors::ErrorCode::PARSE_EXPECTED_OBJECT_PROPERTY_NAME);
  const Token property = consume();
  return std::make_shared<Expression>(MemberExpr{std::move(expression), property.lexeme, nullptr, false},
                                SourceRange{start, property.end});
}

ExprPtr Parser::parseComputedMemberPostfix(ExprPtr expression) {
  (void)consume();
  const SourcePosition start = expression->range.start;
  auto property = parseExpression();
  if (!eat("]"))
    fail("expected ']' after computed property",
         errors::ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_COMPUTED_PROPERTY);
  return std::make_shared<Expression>(MemberExpr{std::move(expression), {}, std::move(property), true},
                                SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parsePrimary() {
  if (current().lexeme == "bytes" && index_ + 1 < tokens_.size()) {
    const Token &next = tokens_[index_ + 1];
    if (next.kind == TokenKind::StringLiteral) {
      const SourcePosition start = consume().start;
      const Token literal = consume();
      return std::make_shared<Expression>(BytesLiteralExpr{literal.text},
                                          SourceRange{start, literal.end});
    }
    const bool bitLiteral = next.kind == TokenKind::NumberLiteral && next.lexeme.size() > 2 &&
                            next.lexeme[0] == '0' &&
                            (next.lexeme[1] == 'b' || next.lexeme[1] == 'B');
    if (bitLiteral) {
      std::string value;
      if (decodeBitsLiteral(next.lexeme, value)) {
        const SourcePosition start = consume().start;
        const Token literal = consume();
        return std::make_shared<Expression>(BytesLiteralExpr{std::move(value)},
                                            SourceRange{start, literal.end});
      }
    }
  }
  if (current().lexeme == "{") return parseObjectLiteral();
  if (current().lexeme == "[") return parseArrayLiteral();
  if (current().lexeme == "(") {
    const SourcePosition start = consume().start;
    auto expression = parseExpression();
    if (!eat(")"))
      fail("expected ')' after expression",
           errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_EXPRESSION);
    expression->range = SourceRange{start, tokens_.at(index_ - 1).end};
    return expression;
  }

  const Token token = consume();
  const SourceRange sourceRange{token.start, token.end};
  if (token.kind == TokenKind::NumberLiteral)
    return std::make_shared<Expression>(NumberLiteralExpr{token.lexeme}, sourceRange);
  if (token.kind == TokenKind::StringLiteral)
    return std::make_shared<Expression>(StringLiteralExpr{token.text}, sourceRange);
  if (token.lexeme == "true" || token.lexeme == "false")
    return std::make_shared<Expression>(BooleanLiteralExpr{token.lexeme == "true"}, sourceRange);
  if (token.lexeme == "null") return std::make_shared<Expression>(NullLiteralExpr{}, sourceRange);
  if (token.lexeme == "undefined")
    return std::make_shared<Expression>(UndefinedLiteralExpr{}, sourceRange);
  if (token.kind == TokenKind::Identifier || token.kind == TokenKind::Keyword) {
    // Reserved keywords that are not supported in expression position produce a clear error
    if (token.kind == TokenKind::Keyword) {
      fail("keyword '" + token.lexeme + "' is not supported in expression position",
           errors::ErrorCode::PARSE_EXPECTED_EXPRESSION);
    }
    return std::make_shared<Expression>(IdentifierExpr{token.lexeme}, sourceRange);
  }
  fail("expected expression", errors::ErrorCode::PARSE_EXPECTED_EXPRESSION);
}

ExprPtr Parser::parseObjectLiteral() {
  const SourcePosition start = consume().start;
  ObjectExpr object;
  if (!eat("}")) {
    do {
      std::string key;
      if (current().kind == TokenKind::StringLiteral) {
        key = consume().text;
      } else if (current().kind == TokenKind::Identifier || current().kind == TokenKind::Keyword ||
                 current().kind == TokenKind::NumberLiteral) {
        key = consume().lexeme;
      } else {
        fail("expected object property name",
             errors::ErrorCode::PARSE_EXPECTED_OBJECT_PROPERTY_NAME);
      }
      if (!eat(":"))
        fail("expected ':' after object property name",
             errors::ErrorCode::PARSE_EXPECTED_COLON_AFTER_OBJECT_PROPERTY_NAME);
      object.properties.push_back({std::move(key), parseExpression()});
    } while (eat(",") && current().lexeme != "}");
    if (!eat("}"))
      fail("expected '}' after object literal",
           errors::ErrorCode::PARSE_EXPECTED_RBRACE_AFTER_OBJECT_LITERAL);
  }
  return std::make_shared<Expression>(std::move(object), SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parseArrayLiteral() {
  const SourcePosition start = consume().start;
  ArrayExpr array;
  if (!eat("]")) {
    do {
      if (current().kind == TokenKind::NumberLiteral && peekNextIsColon()) {
        array.hasDesignated = true;
        std::size_t index = std::stoull(consume().lexeme);
        if (!eat(":"))
          fail("expected ':' after array index",
               errors::ErrorCode::PARSE_EXPECTED_COLON_AFTER_OBJECT_PROPERTY_NAME);
        auto value = parseExpression();
        array.designated.push_back({index, std::move(value)});
      } else {
        array.elements.push_back(parseExpression());
      }
    } while (eat(",") && current().lexeme != "]");
    if (!eat("]"))
      fail("expected ']' after array literal",
           errors::ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_ARRAY_LITERAL);
  }
  return std::make_shared<Expression>(std::move(array), SourceRange{start, tokens_.at(index_ - 1).end});
}

bool Parser::peekNextIsColon() const {
  if (index_ + 1 >= tokens_.size()) return false;
  return tokens_[index_ + 1].lexeme == ":";
}

bool Parser::startsFunctionDeclaration() const {
  std::size_t cursor = index_;
  if (cursor >= tokens_.size()) return false;
  const Token &first = tokens_[cursor];
  const bool typeKeyword =
      first.kind == TokenKind::Keyword &&
      (first.lexeme == "string" || first.lexeme == "bool" ||
       first.lexeme == "boolean" || first.lexeme == "bytes" || first.lexeme == "auto" ||
       first.lexeme == "void" || first.lexeme == "int");
  if (first.kind != TokenKind::Identifier && !typeKeyword) return false;
  ++cursor;

  if (typeKeyword && first.lexeme == "int" && cursor < tokens_.size() &&
      tokens_[cursor].lexeme == "(") {
    int depth = 0;
    while (cursor < tokens_.size()) {
      if (tokens_[cursor].lexeme == "(") {
        ++depth;
      } else if (tokens_[cursor].lexeme == ")") {
        --depth;
        if (depth == 0) {
          ++cursor;
          break;
        }
      }
      ++cursor;
    }
    if (depth != 0) return false;
  }

  cursor = skipSizedIntArguments(tokens_, cursor);

  while (cursor < tokens_.size() &&
         (tokens_[cursor].lexeme == "*" || tokens_[cursor].lexeme == "**"))
    ++cursor;

  if (cursor < tokens_.size() && tokens_[cursor].lexeme == "[") {
    ++cursor;
    if (cursor < tokens_.size() && tokens_[cursor].kind == TokenKind::NumberLiteral) ++cursor;
    if (cursor >= tokens_.size() || tokens_[cursor].lexeme != "]") return false;
    ++cursor;
  }

  while (cursor < tokens_.size() &&
         (tokens_[cursor].lexeme == "|" || tokens_[cursor].lexeme == ",")) {
    ++cursor;
    if (cursor >= tokens_.size()) return false;
    if (tokens_[cursor].kind != TokenKind::Identifier &&
        tokens_[cursor].kind != TokenKind::Keyword)
      return false;
    ++cursor;
    cursor = skipSizedIntArguments(tokens_, cursor);
    while (cursor < tokens_.size() &&
         (tokens_[cursor].lexeme == "*" || tokens_[cursor].lexeme == "**"))
    ++cursor;
  }

  if (cursor + 1 >= tokens_.size()) return false;
  if (tokens_[cursor].kind != TokenKind::Identifier) return false;
  return tokens_[cursor + 1].lexeme == "(";
}

bool Parser::startsVariableDeclaration() const {
  std::size_t cursor = index_;
  if (cursor >= tokens_.size() || tokens_[cursor].kind != TokenKind::Identifier) return false;
  ++cursor;
  cursor = skipSizedIntArguments(tokens_, cursor);

  while (cursor < tokens_.size() &&
         (tokens_[cursor].lexeme == "*" || tokens_[cursor].lexeme == "**"))
    ++cursor;

  if (cursor < tokens_.size() && tokens_[cursor].lexeme == "[") {
    ++cursor;
    if (cursor < tokens_.size() && tokens_[cursor].kind == TokenKind::NumberLiteral) ++cursor;
    if (cursor >= tokens_.size() || tokens_[cursor].lexeme != "]") return false;
    ++cursor;
  }

  while (cursor < tokens_.size() &&
         (tokens_[cursor].lexeme == "|" || tokens_[cursor].lexeme == ",")) {
    ++cursor;
    if (cursor >= tokens_.size()) return false;
    if (tokens_[cursor].kind != TokenKind::Identifier &&
        tokens_[cursor].kind != TokenKind::Keyword)
      return false;
    ++cursor;
    cursor = skipSizedIntArguments(tokens_, cursor);
    while (cursor < tokens_.size() &&
           (tokens_[cursor].lexeme == "*" || tokens_[cursor].lexeme == "**"))
      ++cursor;
  }

  if (cursor >= tokens_.size() || tokens_[cursor].kind != TokenKind::Identifier) return false;
  const std::size_t nameIndex = cursor;
  ++cursor;

  if (cursor < tokens_.size() && tokens_[cursor].lexeme == "[") {
    ++cursor;
    if (cursor < tokens_.size() && tokens_[cursor].kind == TokenKind::NumberLiteral) ++cursor;
    if (cursor >= tokens_.size() || tokens_[cursor].lexeme != "]") return false;
    ++cursor;
  }

  if (cursor >= tokens_.size()) return false;
  if (tokens_[cursor].lexeme == "=") return true;
  return tokens_[cursor].lexeme == ";" &&
         tokens_[cursor].start.line == tokens_[nameIndex].start.line;
}

Statement Parser::parseLabeledStatement() {
  const SourcePosition start = current().start;
  const std::string label = consume().lexeme;
  if (!eat(":"))
    fail("expected ':' after label", errors::ErrorCode::PARSE_EXPECTED_COLON_AFTER_LABEL);
  auto statement = parseStatement();
  return Statement(LabeledStatement{label, std::make_unique<Statement>(std::move(statement))},
                   SourceRange{start, tokens_.at(index_ - 1).end});
}

} // namespace jsmm::reader
