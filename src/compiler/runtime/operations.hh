#pragma once

#include "engine/reference.hh"
#include "engine/value.hh"
#include "compiler/ast.hh"
#include <string>

namespace rma::compiler {

bool truthy(const rma::syntax::Value &value);
std::string toString(const rma::syntax::Value &value);
double number(const rma::syntax::Value &value);
std::uint32_t toUint32(const rma::syntax::Value &value);
std::int32_t toInt32(const rma::syntax::Value &value);

bool looseEqual(const rma::syntax::Value &a, const rma::syntax::Value &b);
bool strictEqual(const rma::syntax::Value &a, const rma::syntax::Value &b);
rma::syntax::Value toPrimitive(const rma::syntax::Value &value);

rma::syntax::PropertyKey toPropertyKey(const rma::syntax::Value &value);

std::string decodeString(const std::string &spelling);
rma::syntax::Value literalValue(const LiteralExpr &literal);

} // namespace rma::compiler
