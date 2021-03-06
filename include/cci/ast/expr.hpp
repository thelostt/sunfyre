#pragma once
#include "cci/ast/arena_types.hpp"
#include "cci/ast/ast_context.hpp"
#include "cci/ast/type.hpp"
#include "cci/langopts.hpp"
#include "cci/syntax/source_map.hpp"
#include "cci/util/span.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace cci::ast {

// Expression categories.
enum class ExprValueKind
{
    LValue,
    RValue,
};

enum class ExprClass
{
    IntegerLiteral,
    CharacterConstant,
    StringLiteral,
    ParenExpr,
    ArraySubscript,
    ImplicitCast,
};

// Expression.
struct Expr
{
private:
    ExprClass ec;
    ExprValueKind vk;
    QualType ty;
    syntax::ByteSpan range;

protected:
    Expr(ExprClass ec, ExprValueKind vk, QualType ty, syntax::ByteSpan r)
        : ec(ec), vk(vk), ty(ty), range(r)
    {}

public:
    auto expr_class() const -> ExprClass { return ec; }
    auto value_kind() const -> ExprValueKind { return vk; }
    auto type() const -> QualType { return ty; }
    auto begin_loc() const -> syntax::ByteLoc { return range.start; }
    auto end_loc() const -> syntax::ByteLoc { return range.end; }
    auto source_span() const -> syntax::ByteSpan { return range; }

    bool is_lvalue() const { return ExprValueKind::LValue == vk; }
    bool is_rvalue() const { return ExprValueKind::RValue == vk; }

    template <typename T>
    auto get_as() const -> const T *
    {
        return T::classof(ec) ? static_cast<const T *>(this) : nullptr;
    }
};

// Numeric constant that is an integer literal.
struct IntegerLiteral : Expr
{
private:
    uint64_t val;

    IntegerLiteral(uint64_t val, QualType ty, syntax::ByteSpan r)
        : Expr(ExprClass::IntegerLiteral, ExprValueKind::RValue, ty, r)
        , val(val)
    {}

public:
    auto value() const -> uint64_t { return val; }

    static auto create(const ASTContext &ctx, uint64_t value, QualType ty,
                       syntax::ByteSpan source_span)
        -> arena_ptr<IntegerLiteral>
    {
        return new (ctx) IntegerLiteral(value, ty, source_span);
    }

    static bool classof(ExprClass ec)
    {
        return ExprClass::IntegerLiteral == ec;
    }
};

enum class CharacterConstantKind
{
    Ascii,
    UTF16,
    UTF32,
    Wide,
};

struct CharacterConstant : Expr
{
private:
    uint32_t val;
    CharacterConstantKind cck;

    CharacterConstant(uint32_t val, CharacterConstantKind cck, QualType ty,
                      syntax::ByteSpan r)
        : Expr(ExprClass::CharacterConstant, ExprValueKind::RValue, ty, r)
        , val(val)
        , cck(cck)
    {}

public:
    auto char_value() const -> uint32_t { return val; }
    auto char_kind() const -> CharacterConstantKind { return cck; }

    static auto create(const ASTContext &ctx, uint32_t value,
                       CharacterConstantKind cck, QualType ty,
                       syntax::ByteSpan source_span)
        -> arena_ptr<CharacterConstant>
    {
        return new (ctx) CharacterConstant(value, cck, ty, source_span);
    }

    static bool classof(ExprClass ec)
    {
        return ExprClass::CharacterConstant == ec;
    }
};

enum class StringLiteralKind
{
    Ascii,
    UTF8,
    UTF16,
    UTF32,
    Wide,
};

struct StringLiteral : Expr
{
private:
    span<std::byte> str_data; ///< String content.
    StringLiteralKind sk;
    size_t char_byte_width; ///< Character's size in bytes.
    span<syntax::ByteLoc> tok_locs; ///< Sequence of each string location

    StringLiteral(QualType ty, span<std::byte> str_data, StringLiteralKind sk,
                  size_t cbw, span<syntax::ByteLoc> locs,
                  syntax::ByteLoc rquote_loc)
        : Expr(ExprClass::StringLiteral, ExprValueKind::LValue, ty,
               syntax::ByteSpan(locs[0], rquote_loc + syntax::ByteLoc(1)))
        , str_data(str_data)
        , sk(sk)
        , char_byte_width(cbw)
        , tok_locs(locs)
    {}

public:
    auto str_kind() const -> StringLiteralKind { return sk; }

    auto string_as_utf8() const -> std::string_view
    {
        cci_expects(char_byte_width == 1);
        auto ptr = reinterpret_cast<const char *>(str_data.data());
        size_t len = str_data.size();
        return {ptr, len};
    }

    auto string_as_bytes() const -> span<const std::byte>
    {
        return as_bytes(str_data);
    }

    auto byte_length() const -> size_t { return str_data.size_bytes(); }
    auto length() const -> size_t { return byte_length() / char_byte_width; }

    static auto create(const ASTContext &ctx, QualType ty,
                       span<std::byte> str_data, StringLiteralKind sk,
                       size_t cbw, span<syntax::ByteLoc> locs,
                       syntax::ByteLoc rquote_loc) -> arena_ptr<StringLiteral>
    {
        cci_expects(!locs.empty());
        return new (ctx) StringLiteral(ty, str_data, sk, cbw, locs, rquote_loc);
    }

    static bool classof(ExprClass ec) { return ExprClass::StringLiteral == ec; }
};

struct ParenExpr : Expr
{
private:
    arena_ptr<Expr> inner_expr;
    syntax::ByteLoc lparen_loc;
    syntax::ByteLoc rparen_loc;

    ParenExpr(arena_ptr<Expr> expr, syntax::ByteLoc lparen,
              syntax::ByteLoc rparen)
        : Expr(ExprClass::ParenExpr, expr->value_kind(), expr->type(),
               syntax::ByteSpan(lparen, rparen + syntax::ByteLoc(1)))
        , inner_expr(expr)
        , lparen_loc(lparen)
        , rparen_loc(rparen)
    {}

public:
    auto sub_expr() const -> arena_ptr<Expr> { return inner_expr; }
    auto open_paren_loc() const -> syntax::ByteLoc { return lparen_loc; }
    auto close_paren_loc() const -> syntax::ByteLoc { return rparen_loc; }

    static auto create(const ASTContext &ctx, arena_ptr<Expr> inner_expr,
                       syntax::ByteLoc lparen, syntax::ByteLoc rparen)
        -> arena_ptr<ParenExpr>
    {
        return new (ctx) ParenExpr(inner_expr, lparen, rparen);
    }

    static bool classof(ExprClass ec) { return ExprClass::ParenExpr == ec; }
};

struct ArraySubscriptExpr : Expr
{
private:
    arena_ptr<Expr> base;
    arena_ptr<Expr> idx;
    syntax::ByteLoc lbracket_loc;

    ArraySubscriptExpr(arena_ptr<Expr> base, arena_ptr<Expr> idx,
                       ExprValueKind vk, QualType ty,
                       syntax::ByteLoc lbracket_loc,
                       syntax::ByteLoc rbracket_loc)
        : Expr(ExprClass::ArraySubscript, vk, ty,
               syntax::ByteSpan(base->begin_loc(),
                                rbracket_loc + syntax::ByteLoc(1)))
        , base(base)
        , idx(idx)
        , lbracket_loc(lbracket_loc)
    {}

public:
    auto base_expr() const -> arena_ptr<Expr> { return base; }
    auto index_expr() const -> arena_ptr<Expr> { return idx; }
    auto open_bracket_loc() const -> syntax::ByteLoc { return lbracket_loc; }

    static auto create(const ASTContext &ctx, arena_ptr<Expr> base_expr,
                       arena_ptr<Expr> index_expr, ExprValueKind vk,
                       QualType ty, syntax::ByteLoc lbracket_loc,
                       syntax::ByteLoc rbracket_loc)
        -> arena_ptr<ArraySubscriptExpr>
    {
        cci_expects(base_expr->type()->get_as<PointerType>() != nullptr);
        cci_expects(index_expr->type()->get_as<PointerType>() == nullptr);
        return new (ctx) ArraySubscriptExpr(base_expr, index_expr, vk, ty,
                                            lbracket_loc, rbracket_loc);
    }

    static bool classof(ExprClass ec)
    {
        return ExprClass::ArraySubscript == ec;
    }
};

enum class CastKind
{
    LValueToRValue,
    ArrayToPointerDecay,
    AtomicToNonAtomic,
};

struct CastExpr : Expr
{
private:
    CastKind ck;
    arena_ptr<Expr> op;

protected:
    CastExpr(ExprClass ec, ExprValueKind vk, QualType ty, CastKind ck,
             arena_ptr<Expr> op, syntax::ByteSpan r)
        : Expr(ec, vk, ty, r), ck(ck), op(op)
    {}

public:
    auto cast_kind() const -> CastKind { return ck; }
    auto operand_expr() const -> arena_ptr<Expr> { return op; }
};

struct ImplicitCastExpr : CastExpr
{
private:
    ImplicitCastExpr(ExprValueKind vk, QualType ty, CastKind ck,
                     arena_ptr<Expr> op)
        : CastExpr(ExprClass::ImplicitCast, vk, ty, ck, op, op->source_span())
    {}

public:
    static auto create(const ASTContext &ctx, ExprValueKind vk, QualType ty,
                       CastKind ck, arena_ptr<Expr> operand)
        -> arena_ptr<ImplicitCastExpr>
    {
        return new (ctx) ImplicitCastExpr(vk, ty, ck, operand);
    }

    static bool classof(ExprClass ec) { return ExprClass::ImplicitCast == ec; }
};

static_assert(std::is_trivially_destructible_v<Expr>);
static_assert(std::is_trivially_destructible_v<IntegerLiteral>);
static_assert(std::is_trivially_destructible_v<CharacterConstant>);
static_assert(std::is_trivially_destructible_v<StringLiteral>);
static_assert(std::is_trivially_destructible_v<ParenExpr>);
static_assert(std::is_trivially_destructible_v<ArraySubscriptExpr>);
static_assert(std::is_trivially_destructible_v<ImplicitCastExpr>);

} // namespace cci::ast
