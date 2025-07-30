#include <assert.h>

import std;
import hlsdk;

import UtlRandom;

using namespace std;

#include "DynExpr.hpp"

void InitializeDynExpr() noexcept
{
	static bool bInit = false;
	if (bInit)
		return;

	DynExpr::BindConstant<Vector>("vecZero", Vector{});
	DynExpr::BindConstant<double>("NaN", std::numeric_limits<double>::quiet_NaN());

	DynExpr::BindFunction("vec3", +[](double x, double y, double z) noexcept { return Vector{ x, y, z }; });
	DynExpr::BindFunction("vec2", +[](double x, double y) noexcept { return Vector2D{ (float)x, (float)y, }; });
	DynExpr::BindMember("x", &Vector2D::x);
	DynExpr::BindMember("y", &Vector2D::y);
	DynExpr::BindMember("x", &Vector::x);
	DynExpr::BindMember("y", &Vector::y);
	DynExpr::BindMember("z", &Vector::z);

	DynExpr::BindMethod("len", &Vector::Length);
	DynExpr::BindMethod("len", &Vector2D::Length);

	DynExpr::BindMethod("vec2", &Vector::Make2D);
	DynExpr::BindFunction("vec3", +[](Vector2D v2) { return Vector{ v2, 0 }; });
	DynExpr::BindFunction("vec3", +[](Vector2D v2, double z) { return Vector{ v2, z }; });

	DynExpr::BindOperator("-", EAssociativity::Right, OpPrec_Unary, +[](double operand) { return -operand; });
	DynExpr::BindOperator("+", EAssociativity::Left, OpPrec_Addition, +[](double lhs, double rhs) { return lhs + rhs; });
	DynExpr::BindOperator("-", EAssociativity::Left, OpPrec_Addition, +[](double lhs, double rhs) { return lhs - rhs; });
	DynExpr::BindOperator("*", EAssociativity::Left, OpPrec_Multiplication, +[](double lhs, double rhs) { return lhs * rhs; });
	DynExpr::BindOperator("/", EAssociativity::Left, OpPrec_Multiplication, +[](double lhs, double rhs) { return lhs / rhs; });
	DynExpr::BindOperator("%", EAssociativity::Left, OpPrec_Multiplication, static_cast<double (*)(double, double)>(&std::fmod));
	DynExpr::BindOperator("**", EAssociativity::Right, OpPrec_PointToMember, +[](double lhs, double rhs) { return std::pow(lhs, rhs); });

	DynExpr::BindOperator("-", EAssociativity::Right, OpPrec_Unary, +[](Vector operand) { return -operand; });
	DynExpr::BindOperator("+", EAssociativity::Left, OpPrec_Addition, +[](Vector lhs, Vector rhs) { return lhs + rhs; });
	DynExpr::BindOperator("-", EAssociativity::Left, OpPrec_Addition, +[](Vector lhs, Vector rhs) { return lhs - rhs; });
	DynExpr::BindOperator("*", EAssociativity::Left, OpPrec_Multiplication, +[](Vector lhs, double rhs) { return lhs * rhs; });
	DynExpr::BindOperator("*", EAssociativity::Left, OpPrec_Multiplication, +[](double lhs, Vector rhs) { return lhs * rhs; });
	DynExpr::BindOperator("/", EAssociativity::Left, OpPrec_Multiplication, +[](Vector lhs, double rhs) { return lhs / rhs; });

	// Bitwise
	DynExpr::BindOperator("&", EAssociativity::Left, OpPrec_BitAND, +[](double lhs, double rhs) -> double { return std::lround(lhs) & std::lround(rhs); });
	DynExpr::BindOperator("|", EAssociativity::Left, OpPrec_BitOR, +[](double lhs, double rhs) -> double { return std::lround(lhs) | std::lround(rhs); });
	DynExpr::BindOperator("^", EAssociativity::Left, OpPrec_BitXOR, +[](double lhs, double rhs) -> double { return std::lround(lhs) ^ std::lround(rhs); });
	DynExpr::BindOperator("~", EAssociativity::Right, OpPrec_Unary, +[](double operand) -> double { return ~std::lround(operand); });
	DynExpr::BindOperator("<<", EAssociativity::Left, OpPrec_BitShift, +[](double lhs, double rhs) -> double { return std::lround(lhs) << std::lround(rhs); });
	DynExpr::BindOperator(">>", EAssociativity::Left, OpPrec_BitShift, +[](double lhs, double rhs) -> double { return std::lround(lhs) >> std::lround(rhs); });

	// Random
	DynExpr::BindFunction("rd", &UTIL_Random<double>);
	DynExpr::BindFunction("rand", &UTIL_Random<double>);
	DynExpr::BindFunction("random", &UTIL_Random<double>);
	DynExpr::BindFunction("randi", +[](double low, double high) { return (double)UTIL_Random(std::lround(low), std::lround(high)); });

	// Basic
	DynExpr::BindFunction("abs", static_cast<double (*)(double)>(&std::abs));
	DynExpr::BindFunction("rem", static_cast<double (*)(double, double)>(&std::fmod));
	DynExpr::BindFunction("quot", +[](double dividend, double divisor) { auto r = std::div((int32_t)dividend, (int32_t)divisor); return r.quot; });
	DynExpr::BindFunction("max", static_cast<double (*)(double, double)>(&std::fmax));
	DynExpr::BindFunction("min", static_cast<double (*)(double, double)>(&std::fmin));
	DynExpr::BindFunction("clamp", &std::clamp<double>);

	// Rounding
	DynExpr::BindFunction("ceil", static_cast<double (*)(double)>(&std::ceil));
	DynExpr::BindFunction("floor", static_cast<double (*)(double)>(&std::floor));
	DynExpr::BindFunction("round", static_cast<double (*)(double)>(&std::round));

	// Trigonometric
	DynExpr::BindFunction("sin", static_cast<double (*)(double)>(&std::sin));
	DynExpr::BindFunction("cos", static_cast<double (*)(double)>(&std::cos));
	DynExpr::BindFunction("tan", static_cast<double (*)(double)>(&std::tan));
	DynExpr::BindFunction("asin", static_cast<double (*)(double)>(&std::asin));
	DynExpr::BindFunction("acos", static_cast<double (*)(double)>(&std::acos));
	DynExpr::BindFunction("atan", static_cast<double (*)(double)>(&std::atan));

	// Linear Algebra
	DynExpr::BindFunction("hypot", static_cast<double (*)(double, double)>(&std::hypot));
	DynExpr::BindFunction("hypot", static_cast<double (*)(double, double, double)>(&std::hypot));
	DynExpr::BindOperator(u8"⋅", EAssociativity::Left, OpPrec_Multiplication, +[](Vector lhs, Vector rhs) { return DotProduct(lhs, rhs); });
	DynExpr::BindOperator(u8"⋅", EAssociativity::Left, OpPrec_Multiplication, +[](Vector2D lhs, Vector2D rhs) { return DotProduct(lhs, rhs); });
	DynExpr::BindOperator(u8"×", EAssociativity::Left, OpPrec_Multiplication, &CrossProduct);
	DynExpr::BindOperator(u8"×", EAssociativity::Left, OpPrec_Multiplication, +[](Vector2D lhs, Vector2D rhs) { return CrossProduct({ lhs, 0 }, { rhs, 0 }); });

	// Exponential
	DynExpr::BindFunction("pow", static_cast<double (*)(double, double)>(&std::pow));
	DynExpr::BindFunction("log", +[](double base, double x) { return std::log(x) / std::log(base); });
	DynExpr::BindFunction("sqrt", static_cast<double (*)(double)>(&std::sqrt));
	DynExpr::BindFunction("cbrt", static_cast<double (*)(double)>(&std::cbrt));

	bInit = true;
}

auto RunDynExpr(string_view szExpr) noexcept -> expected<any, string>
{
	return
		DynExpr::Tokenizer(szExpr)
		.or_else([](error_t const& err) noexcept -> expected<vector<string_view>, string> { return std::unexpected(err.ToString()); })
		.and_then(&DynExpr::ShuntingYardAlgorithm)
		.and_then(&DynExpr::Execute<any>);
}

void DynExprBindVector(string_view name, Vector const& vec) noexcept
{
	DynExpr::BindConstant<Vector>(name, vec);
}

void DynExprBindNum(string_view name, double num) noexcept
{
	DynExpr::BindConstant<double>(name, num);
}

void DynExprUnbind(string_view name) noexcept
{
	DynExpr::BindConstant<void>(name);
}
