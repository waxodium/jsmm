#include "reader/parser.hh"

#include "errors/errors.hh"

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace rma::reader {

const std::unordered_map<std::string, Parser::InfixParselet> &Parser::infixParselets() {
  using P = Parser;
  static const std::unordered_map<std::string, P::InfixParselet> table{
      {"=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"+=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"-=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"*=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"/=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"%=", {{1, true}, &P::parseAssignmentInfix, true}},
      {"||", {{2, false}, &P::parseBinaryInfix}},
      {"??", {{2, false}, &P::parseBinaryInfix}},
      {"&&", {{3, false}, &P::parseBinaryInfix}},
      {"|", {{4, false}, &P::parseBinaryInfix}},
      {"^", {{5, false}, &P::parseBinaryInfix}},
      {"&", {{6, false}, &P::parseBinaryInfix}},
      {"==", {{7, false}, &P::parseBinaryInfix}},
      {"!=", {{7, false}, &P::parseBinaryInfix}},
      {"===", {{7, false}, &P::parseBinaryInfix}},
      {"!==", {{7, false}, &P::parseBinaryInfix}},
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
      {"%", {{11, false}, &P::parseBinaryInfix}},
      {"**", {{12, true}, &P::parseBinaryInfix}}};
  return table;
}

const std::unordered_map<std::string, Parser::PrefixParser> &Parser::prefixParselets() {
  using P = Parser;
  static const std::unordered_map<std::string, P::PrefixParser> table{
      {"!", &P::parseUnaryPrefix},      {"~", &P::parseUnaryPrefix},
      {"+", &P::parseUnaryPrefix},      {"-", &P::parseUnaryPrefix},
      {"++", &P::parseUpdatePrefix},    {"--", &P::parseUpdatePrefix},
      {"typeof", &P::parseUnaryPrefix}, {"void", &P::parseUnaryPrefix},
      {"delete", &P::parseUnaryPrefix}};
  return table;
}

const std::unordered_map<std::string, Parser::PostfixParser> &Parser::postfixParselets() {
  using P = Parser;
  static const std::unordered_map<std::string, P::PostfixParser> table{
      {"(", &P::parseCallPostfix},
      {".", &P::parseMemberPostfix},
      {"[", &P::parseComputedMemberPostfix},
      {"++", &P::parseUpdatePostfix},
      {"--", &P::parseUpdatePostfix}};
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

Program Parser::parse() {
  Program program;
  while (current().kind != TokenKind::End) program.statements.push_back(parseStatement());
  return program;
}

Statement Parser::parseStatement() {
  const SourcePosition start = current().start;
  Statement statement = [&]() {
    if (current().kind == TokenKind::Keyword) {
      const std::string &keyword = current().lexeme;
      if (keyword == "let" || keyword == "const" || keyword == "var") return parseDeclaration();
      if (keyword == "function") return parseFunctionDeclaration();
      if (keyword == "return") return parseReturnStatement();
      if (keyword == "if") return parseIfStatement();
      if (keyword == "while") return parseWhileStatement();
      if (keyword == "for") return parseForStatement();
      if (keyword == "break") return parseBreakStatement();
      if (keyword == "continue") return parseContinueStatement();
      fail("unsupported keyword '" + keyword + "'",
           errors::ErrorCode::PARSE_UNSUPPORTED_KEYWORD);
    }
    if (current().lexeme == "{") return parseBlock();
    if (current().lexeme == ";") return parseEmptyStatement();
    return parseExpressionStatement();
  }();
  statement.range = SourceRange{start, tokens_.at(index_ == 0 ? 0 : index_ - 1).end};
  return statement;
}

Statement Parser::parseEmptyStatement() {
  const SourcePosition start = current().start;
  consume();
  return Statement(BlockStatement{}, SourceRange{start, tokens_.at(index_ - 1).end});
}

Statement Parser::parseExpressionStatement() {
  auto expression = parseExpression();
  optionalSemicolon();
  return Statement(ExpressionStatement{std::move(expression)});
}

Statement Parser::parseDeclaration() {
  const std::string kind = consume().lexeme;
  VariableDeclaration declaration{kind, {}};
  do {
    if (current().kind != TokenKind::Identifier)
      fail("expected binding name", errors::ErrorCode::PARSE_EXPECTED_BINDING_NAME);
    const std::string name = consume().lexeme;
    ExprPtr initializer;
    if (eat("=")) {
      initializer = parseExpression();
    } else if (kind == "const") {
      fail("const declaration requires an initializer",
           errors::ErrorCode::PARSE_CONST_REQUIRES_INITIALIZER);
    }
    declaration.declarations.push_back({std::move(name), std::move(initializer)});
  } while (eat(","));
  optionalSemicolon();
  return Statement(std::move(declaration));
}

Statement Parser::parseFunctionDeclaration() {
  consume();
  if (current().kind != TokenKind::Identifier)
    fail("expected function name", errors::ErrorCode::PARSE_EXPECTED_FUNCTION_NAME);
  FunctionDeclaration function{consume().lexeme, {}, {}};
  if (!eat("("))
    fail("expected '(' after function name",
         errors::ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_FUNCTION_NAME);
  if (!eat(")")) {
    do {
      if (current().kind != TokenKind::Identifier)
        fail("expected parameter name", errors::ErrorCode::PARSE_EXPECTED_PARAMETER_NAME);
      function.parameters.push_back(consume().lexeme);
    } while (eat(","));
    if (!eat(")"))
      fail("expected ')' after parameters",
           errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_PARAMETERS);
  }
  if (!eat("{"))
    fail("expected '{' before function body",
         errors::ErrorCode::PARSE_EXPECTED_LBRACE_BEFORE_FUNCTION_BODY);
  function.body = parseBlockBody();
  return Statement(std::move(function));
}

Statement Parser::parseReturnStatement() {
  consume();
  ExprPtr argument;
  if (current().lexeme != ";" && current().lexeme != "}" && current().kind != TokenKind::End)
    argument = parseExpression();
  optionalSemicolon();
  return Statement(ReturnStatement{std::move(argument)});
}

Statement Parser::parseIfStatement() {
  consume();
  if (!eat("(")) fail("expected '(' after 'if'", errors::ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_IF);
  auto condition = parseExpression();
  if (!eat(")"))
    fail("expected ')' after if condition", errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_IF_CONDITION);
  if (!eat("{"))
    fail("expected '{' for if consequent", errors::ErrorCode::PARSE_EXPECTED_LBRACE_FOR_IF_CONSEQUENT);
  StatementList consequent = parseBlockBody();
  StatementList alternate;
  if (eat("else")) {
    if (eat("{")) {
      alternate = parseBlockBody();
    } else {
      alternate.push_back(parseStatement());
    }
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
  if (!eat("{"))
    fail("expected '{' for while body", errors::ErrorCode::PARSE_EXPECTED_LBRACE_FOR_WHILE_BODY);
  StatementList body = parseBlockBody();
  return Statement(WhileStatement{std::move(condition), std::move(body)});
}

Statement Parser::parseForStatement() {
  consume();
  if (!eat("(")) fail("expected '(' after 'for'", errors::ErrorCode::PARSE_EXPECTED_LPAREN_AFTER_FOR);
  std::unique_ptr<VariableDeclaration> init;
  if (current().lexeme == "let" || current().lexeme == "const" || current().lexeme == "var") {
    const std::string kind = consume().lexeme;
    VariableDeclaration declaration{kind, {}};
    do {
      if (current().kind != TokenKind::Identifier)
        fail("expected binding name", errors::ErrorCode::PARSE_EXPECTED_BINDING_NAME);
      const std::string name = consume().lexeme;
      ExprPtr initializer;
      if (eat("=")) {
        initializer = parseExpression();
      } else if (kind == "const") {
        fail("const declaration requires an initializer",
             errors::ErrorCode::PARSE_CONST_REQUIRES_INITIALIZER);
      }
      declaration.declarations.push_back({std::move(name), std::move(initializer)});
    } while (eat(","));
    init = std::make_unique<VariableDeclaration>(std::move(declaration));
  } else if (current().lexeme != ";") {
    (void)parseExpression();
  }
  if (!eat(";"))
    fail("expected ';' after for-init", errors::ErrorCode::PARSE_EXPECTED_SEMICOLON_AFTER_FOR_INIT);
  ExprPtr condition;
  if (current().lexeme != ";") condition = parseExpression();
  if (!eat(";"))
    fail("expected ';' after for-condition",
         errors::ErrorCode::PARSE_EXPECTED_SEMICOLON_AFTER_FOR_CONDITION);
  ExprPtr update;
  if (current().lexeme != ")") update = parseExpression();
  if (!eat(")"))
    fail("expected ')' after for clauses", errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_FOR_CLAUSES);
  if (!eat("{"))
    fail("expected '{' for for body", errors::ErrorCode::PARSE_EXPECTED_LBRACE_FOR_FOR_BODY);
  StatementList body = parseBlockBody();
  return Statement(
      ForStatement{std::move(init), std::move(condition), std::move(update), std::move(body)});
}

Statement Parser::parseBreakStatement() {
  const SourcePosition start = current().start;
  consume();
  optionalSemicolon();
  return Statement(BreakStatement{}, SourceRange{start, tokens_.at(index_ - 1).end});
}

Statement Parser::parseContinueStatement() {
  const SourcePosition start = current().start;
  consume();
  optionalSemicolon();
  return Statement(ContinueStatement{}, SourceRange{start, tokens_.at(index_ - 1).end});
}

Statement Parser::parseBlock() {
  consume();
  return Statement(BlockStatement{parseBlockBody()});
}

StatementList Parser::parseBlockBody() {
  StatementList statements;
  while (current().kind != TokenKind::End && current().lexeme != "}")
    statements.push_back(parseStatement());
  if (!eat("}")) fail("expected '}' to close block",
                      errors::ErrorCode::PARSE_EXPECTED_RBRACE_TO_CLOSE_BLOCK);
  return statements;
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
  return std::make_shared<Expr>(UnaryExpr{op, std::move(operand)},
                                SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parseUpdatePrefix() {
  const SourcePosition start = current().start;
  const std::string op = consume().lexeme;
  auto target = parsePrefix();
  return std::make_shared<Expr>(UpdateExpr{op, true, std::move(target)},
                                SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parseUpdatePostfix(ExprPtr expression) {
  const SourcePosition start = expression->range.start;
  const std::string op = consume().lexeme;
  return std::make_shared<Expr>(UpdateExpr{op, false, std::move(expression)},
                                SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parseBinaryInfix(ExprPtr left, const std::string &op, OperatorInfo info) {
  const SourcePosition start = left->range.start;
  auto right = parseExpression(info.bindingPower + (info.rightAssociative ? 0 : 1));
  const SourceRange sourceRange{start, right->range.end};
  return std::make_shared<Expr>(BinaryExpr{op, std::move(left), std::move(right)}, sourceRange);
}

ExprPtr Parser::parseAssignmentInfix(ExprPtr left, const std::string &op, OperatorInfo info) {
  const SourcePosition start = left->range.start;
  auto right = parseExpression(info.bindingPower);
  const SourceRange sourceRange{start, right->range.end};
  return std::make_shared<Expr>(AssignmentExpr{op, std::move(left), std::move(right)}, sourceRange);
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
  std::vector<ExprPtr> arguments;
  if (!eat(")")) {
    do { arguments.push_back(parseExpression()); } while (eat(","));
    if (!eat(")")) fail("expected ')' after call arguments",
                        errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_CALL_ARGUMENTS);
  }
  return std::make_shared<Expr>(CallExpr{std::move(expression), std::move(arguments)},
                                SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parseMemberPostfix(ExprPtr expression) {
  (void)consume();
  const SourcePosition start = expression->range.start;
  const Token property = consume();
  if (property.kind != TokenKind::Identifier && property.kind != TokenKind::Keyword)
    fail("expected property name after '.'",
         errors::ErrorCode::PARSE_EXPECTED_PROPERTY_NAME_AFTER_DOT);
  return std::make_shared<Expr>(MemberExpr{std::move(expression), property.lexeme, {}, false},
                                SourceRange{start, property.end});
}

ExprPtr Parser::parseComputedMemberPostfix(ExprPtr expression) {
  (void)consume();
  const SourcePosition start = expression->range.start;
  auto property = parseExpression();
  if (!eat("]")) fail("expected ']' after computed property",
                      errors::ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_COMPUTED_PROPERTY);
  return std::make_shared<Expr>(MemberExpr{std::move(expression), {}, std::move(property), true},
                                SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parsePrimary() {
  if (current().lexeme == "{") return parseObjectLiteral();
  if (current().lexeme == "[") return parseArrayLiteral();
  if (current().lexeme == "(") {
    const SourcePosition start = consume().start;
    auto expression = parseExpression();
    if (!eat(")"))
      fail("expected ')' after expression", errors::ErrorCode::PARSE_EXPECTED_RPAREN_AFTER_EXPRESSION);
    expression->range = SourceRange{start, tokens_.at(index_ - 1).end};
    return expression;
  }

  const Token token = consume();
  const SourceRange sourceRange{token.start, token.end};
  if (token.kind == TokenKind::NumberLiteral)
    return std::make_shared<Expr>(NumberLiteralExpr{token.lexeme}, sourceRange);
  if (token.kind == TokenKind::StringLiteral)
    return std::make_shared<Expr>(StringLiteralExpr{token.text}, sourceRange);
  if (token.lexeme == "true" || token.lexeme == "false")
    return std::make_shared<Expr>(BooleanLiteralExpr{token.lexeme == "true"}, sourceRange);
  if (token.lexeme == "null") return std::make_shared<Expr>(NullLiteralExpr{}, sourceRange);
  if (token.lexeme == "undefined")
    return std::make_shared<Expr>(UndefinedLiteralExpr{}, sourceRange);
  if (token.kind == TokenKind::Identifier)
    return std::make_shared<Expr>(IdentifierExpr{token.lexeme}, sourceRange);
  if (token.kind == TokenKind::Keyword)
    fail("unsupported keyword '" + token.lexeme + "'",
         errors::ErrorCode::PARSE_UNSUPPORTED_KEYWORD);
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
    if (!eat("}")) fail("expected '}' after object literal",
                        errors::ErrorCode::PARSE_EXPECTED_RBRACE_AFTER_OBJECT_LITERAL);
  }
  return std::make_shared<Expr>(std::move(object), SourceRange{start, tokens_.at(index_ - 1).end});
}

ExprPtr Parser::parseArrayLiteral() {
  const SourcePosition start = consume().start;
  ArrayExpr array;
  if (!eat("]")) {
    do { array.elements.push_back(parseExpression()); } while (eat(",") && current().lexeme != "]");
    if (!eat("]")) fail("expected ']' after array literal",
                        errors::ErrorCode::PARSE_EXPECTED_RBRACKET_AFTER_ARRAY_LITERAL);
  }
  return std::make_shared<Expr>(std::move(array), SourceRange{start, tokens_.at(index_ - 1).end});
}

} // namespace rma::reader
