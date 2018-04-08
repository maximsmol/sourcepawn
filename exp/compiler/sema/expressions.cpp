// vim: set ts=2 sw=2 tw=99 et:
// 
// Copyright (C) 2012-2014 AlliedModders LLC and David Anderson
// 
// This file is part of SourcePawn.
// 
// SourcePawn is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
// 
// SourcePawn is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License along with
// SourcePawn. If not, see http://www.gnu.org/licenses/.
#include "compile-context.h"
#include "semantic-analysis.h"
#include "scopes.h"
#include "symbols.h"
#include "coercion.h"
#include <amtl/am-linkedlist.h>

namespace sp {

using namespace ke;
using namespace ast;

sema::Expr*
SemanticAnalysis::visitExpression(Expression* node)
{
  switch (node->kind()) {
    case AstKind::kIntegerLiteral:
      return visitIntegerLiteral(node->toIntegerLiteral());
    case AstKind::kBinaryExpression:
      return visitBinaryExpression(node->toBinaryExpression());
    case AstKind::kCallExpression:
      return visitCallExpression(node->toCallExpression());
    case AstKind::kNameProxy:
      return visitNameProxy(node->toNameProxy());
    case AstKind::kUnaryExpression:
      return visitUnaryExpression(node->toUnaryExpression());
    case AstKind::kStringLiteral:
      return visitStringLiteral(node->toStringLiteral());
    case AstKind::kIncDecExpression:
      return visitIncDec(node->toIncDecExpression());
    case AstKind::kIndexExpression:
      return visitIndex(node->toIndexExpression());
    default:
      cc_.report(node->loc(), rmsg::unimpl_kind) <<
        "sema-visit-expr" << node->kindName();
      return nullptr;
  }
  return nullptr;
}

sema::CallExpr*
SemanticAnalysis::visitCallExpression(CallExpression* node)
{
  // Call expressions are complicated because we only support very specific
  // patterns. We sniff them out here.
  sema::Expr* callee = nullptr;
  if (NameProxy* proxy = node->callee()->asNameProxy()) {
    if (FunctionSymbol* sym = proxy->sym()->asFunction()) {
      assert(sym->scope()->kind() == Scope::Global);
      callee = new (pool_) sema::NamedFunctionExpr(proxy, sym->impl()->signature_type(), sym);
    }
  }

  if (!callee || !callee->type()->isFunction()) {
    cc_.report(node->loc(), rmsg::callee_is_not_function);
    return nullptr;
  }

  FunctionType* fun_type = callee->type()->asFunction();
  ast::FunctionSignature* sig = fun_type->signature();
  ast::ParameterList* params = sig->parameters();

  ast::ExpressionList* ast_args = node->arguments();

  if (params->length() != ast_args->length()) {
    cc_.report(node->loc(), rmsg::argcount_not_supported);
    return nullptr;
  }

  // :TODO: coerce here.
  sema::ExprList* args = new (pool_) sema::ExprList();
  for (size_t i = 0; i < ast_args->length(); i++) {
    ast::Expression* ast_arg = ast_args->at(i);

    VarDecl* param = params->at(i);
    VariableSymbol* sym = param->sym();

    EvalContext ec(CoercionKind::Arg, ast_arg, sym->type());
    if (!coerce(ec))
      return nullptr;
    args->append(ec.result);
  }

  return new (pool_) sema::CallExpr(node, fun_type->returnType(), callee, args);
}

sema::ConstValueExpr*
SemanticAnalysis::visitIntegerLiteral(IntegerLiteral* node)
{
  // :TODO: test overflow
  int32_t value;
  if (!IntValue::SafeCast(node->value(), &value)) {
    cc_.report(node->loc(), rmsg::int_literal_out_of_range);
    return nullptr;
  }

  BoxedValue b(IntValue::FromValue(value));

  Type* i32type = types_->getPrimitive(PrimitiveType::Int32);
  return new (pool_) sema::ConstValueExpr(node, i32type, b);
}

sema::Expr*
SemanticAnalysis::visitNameProxy(ast::NameProxy* node)
{
  Symbol* base_sym = node->sym();
  VariableSymbol* sym = base_sym->asVariable();
  if (!sym) {
    cc_.report(node->loc(), rmsg::unimpl_kind) <<
      "name-proxy-symbol" << node->kindName();
    return nullptr;
  }

  return new (pool_) sema::VarExpr(node, sym->type(), sym);
}

sema::BinaryExpr*
SemanticAnalysis::visitBinaryExpression(BinaryExpression* node)
{
  sema::Expr* left = visitExpression(node->left());
  if (!left)
    return nullptr;

  sema::Expr* right = visitExpression(node->right());
  if (!right)
    return nullptr;

  // Logical operators need booleans on both sides.
  EvalContext ec_left, ec_right;
  if (node->token() == TOK_OR || node->token() == TOK_AND) {
    ec_left = TestEvalContext(cc_, left);
    ec_right = TestEvalContext(cc_, right);
  } else {
    Type* int32Type = types_->getPrimitive(PrimitiveType::Int32);
    ec_left = EvalContext(CoercionKind::Expr, left, int32Type);
    ec_right = EvalContext(CoercionKind::Expr, right, int32Type);
  }

  if (!coerce(ec_left) || !coerce(ec_right))
    return nullptr;
  left = ec_left.result;
  right = ec_right.result;

  assert(left->type() == right->type());

  Type* type = nullptr;
  switch (node->token()) {
    case TOK_PLUS:
    case TOK_MINUS:
    case TOK_STAR:
    case TOK_SLASH:
    case TOK_PERCENT:
    case TOK_AMPERSAND:
    case TOK_BITOR:
    case TOK_BITXOR:
    case TOK_SHR:
    case TOK_USHR:
    case TOK_SHL:
      type = left->type();
      break;
    case TOK_EQUALS:
    case TOK_NOTEQUALS:
    case TOK_GT:
    case TOK_GE:
    case TOK_LT:
    case TOK_LE:
    case TOK_OR:
    case TOK_AND:
      type = types_->getBool();
      break;
    default:
      cc_.report(node->loc(), rmsg::unimpl_kind) <<
        "sema-bin-token" << TokenNames[node->token()];
      return nullptr;
  }

  return new (pool_) sema::BinaryExpr(node, type, node->token(), left, right);
}

sema::Expr*
SemanticAnalysis::visitUnaryExpression(ast::UnaryExpression* node)
{
  EvalContext ec;
  if (node->token() == TOK_NOT) {
    ec = TestEvalContext(cc_, node->expression());
  } else {
    Type* type = types_->getPrimitive(PrimitiveType::Int32);
    ec = EvalContext(CoercionKind::Expr, node->expression(), type);
  }

  if (!coerce(ec))
    return nullptr;

  return new (pool_) sema::UnaryExpr(node, ec.to, node->token(), ec.result);
}

sema::Expr*
SemanticAnalysis::visitIndex(ast::IndexExpression* node)
{
  sema::Expr* base = visitExpression(node->left());
  if (!base)
    return nullptr;

  if (!base->type()->isArray()) {
    cc_.report(base->src()->loc(), rmsg::cannot_index_type) <<
      base->type();
    return nullptr;
  }

  // Convert the base to an r-value.
  EvalContext base_ec(CoercionKind::RValue, base, base->type());
  if (!coerce(base_ec))
    return nullptr;
  base = base_ec.result;

  // Make sure the index is an integer.
  Type* int32Type = types_->getPrimitive(PrimitiveType::Int32);
  EvalContext index_ec(CoercionKind::Index, node->right(), int32Type);
  if (!coerce(index_ec))
    return nullptr;

  sema::Expr* index = index_ec.result;
  ArrayType* array = base->type()->toArray();

  int32_t value;
  if (index->getConstantInt32(&value)) {
    if (value < 0) {
      cc_.report(index->src()->loc(), rmsg::index_must_be_positive);
      return nullptr;
    }
    if (array->hasFixedLength() && value >= array->fixedLength()) {
      cc_.report(index->src()->loc(), rmsg::index_out_of_bounds);
      return nullptr;
    }
  }

  return new (pool_) sema::IndexExpr(node, array->contained(), base, index);
}

sema::Expr*
SemanticAnalysis::visitStringLiteral(ast::StringLiteral* node)
{
  Type* charType = types_->getPrimitive(PrimitiveType::Char);
  Type* constCharType = types_->newQualified(charType, Qualifiers::Const);
  Type* strLitType = types_->newArray(constCharType, node->arrayLength());

  return new (pool_) sema::StringExpr(node, strLitType, node->literal());
}

// :TODO: write tests for weird operators on weird types, like ++function or function - function.

sema::Expr*
SemanticAnalysis::visitIncDec(ast::IncDecExpression* node)
{
  sema::LValueExpr* expr = visitLValue(node->expression());
  if (!expr)
    return nullptr;

  Type* type = expr->storedType();
  Type* int32Type = types_->getPrimitive(PrimitiveType::Int32);

  if (type->isConst()) {
    cc_.report(node->loc(), rmsg::lvalue_is_const);
    return nullptr;
  }
  if (type != int32Type) {
    cc_.report(node->loc(), rmsg::unimpl_kind) <<
      "sema-incdec" << type;
    return nullptr;
  }

  // :TODO: check const-ness
  return new (pool_) sema::IncDecExpr(node, type, node->token(), expr, node->postfix());
}

sema::LValueExpr*
SemanticAnalysis::visitLValue(ast::Expression* node)
{
  sema::Expr* expr = visitExpression(node);
  if (!expr)
    return nullptr;

  sema::LValueExpr* lv = expr->asLValueExpr();
  if (!lv) {
    cc_.report(node->loc(), rmsg::illegal_lvalue);
    return nullptr;
  }

  return lv;
}

sema::Expr*
SemanticAnalysis::initializer(ast::Expression* node, Type* type)
{
  if (StructInitializer* init = node->asStructInitializer())
    return struct_initializer(init, type);

  sema::Expr* expr = visitExpression(node);
  if (!expr)
    return nullptr;

  // :TODO: check overflow integers
  EvalContext ec(CoercionKind::Assignment, expr, type);
  if (!coerce(ec))
    return nullptr;
  return ec.result;
}

sema::Expr*
SemanticAnalysis::struct_initializer(ast::StructInitializer* expr, Type* type)
{
  if (!type->isStruct()) {
    cc_.report(expr->loc(), rmsg::struct_init_needs_struct_type);
    return nullptr;
  }

  LinkedList<NameAndValue*> entries;
  for (NameAndValue* item : *expr->pairs())
    entries.append(item);

  StructType* st = type->asStruct();
  ast::RecordDecl* decl = st->decl();
  ast::LayoutDecls* body = decl->body();

  PoolList<sema::Expr*>* out = new (pool_) PoolList<sema::Expr*>();

  size_t nfields = 0;
  for (ast::LayoutDecl* decl : *body) {
    FieldDecl* field = decl->asFieldDecl();
    if (!field)
      continue;

    nfields++;

    FieldSymbol* sym = field->sym();
    NameAndValue* assignment = nullptr;

    /* Find a matching assignment. */
    auto iter = entries.begin();
    while (iter != entries.end()) {
      NameAndValue* nv = (*iter);
      if (nv->name() == sym->name()) {
        if (assignment) {
          cc_.report(nv->expr()->loc(), rmsg::struct_init_appears_twice) <<
            nv->name();
        }

        assignment = nv;
        iter = entries.erase(iter);
      } else {
        iter++;
      }
    }

    // The backend must generate a default initializer.
    if (!assignment) {
      out->append(nullptr);
      continue;
    }

    // We only support two types here: int, and string.
    sema::Expr* value = visitExpression(assignment->expr());
    if (!value)
      continue;

    if (sym->type()->isString()) {
      sema::StringExpr* str = value->asStringExpr();
      if (!str) {
        cc_.report(value->src()->loc(), rmsg::struct_init_needs_string_lit) <<
          sym->name();
        continue;
      }
    } else if (sym->type()->isPrimitive(PrimitiveType::Int32)) {
      sema::ConstValueExpr* cv = value->asConstValueExpr();
      if (!cv ||
          !cv->value().isInteger() ||
          !cv->value().toInteger().valueFitsInInt32())
      {
        cc_.report(value->src()->loc(), rmsg::struct_init_needs_string_lit) <<
          sym->name();
        continue;
      }
    } else {
      cc_.report(decl->loc(), rmsg::struct_unsupported_type) <<
        sym->name() << sym->type();
      continue;
    }

    out->append(value);
  }

  for (NameAndValue* nv : entries) {
    cc_.report(nv->loc(), rmsg::struct_field_not_found) <<
      st->name() << nv->name();
  }

  if (out->length() != nfields)
    return nullptr;

  return new (pool_) sema::StructInitExpr(expr, st, out);
}


} // namespace sp
