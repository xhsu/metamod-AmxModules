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

	// Should have be built-in
	DynExpr::BindOperator(",", EAssociativity::Left, 1, +[]() noexcept {});

	DynExpr::BindFunction("Vector", +[](double x, double y, double z) noexcept { return Vector{ x, y, z }; });
	DynExpr::BindFunction("Vector2D", +[](double x, double y) noexcept { return Vector2D{ (float)x, (float)y, }; });
	DynExpr::BindMember("x", &Vector2D::x);
	DynExpr::BindMember("y", &Vector2D::y);
	DynExpr::BindMember("x", &Vector::x);
	DynExpr::BindMember("y", &Vector::y);
	DynExpr::BindMember("z", &Vector::z);

	DynExpr::BindOperator("+", EAssociativity::Left, 3, +[](double lhs, double rhs) { return lhs + rhs; });
	DynExpr::BindOperator("-", EAssociativity::Left, 3, +[](double lhs, double rhs) { return lhs - rhs; });
	DynExpr::BindOperator("*", EAssociativity::Left, 5, +[](double lhs, double rhs) { return lhs * rhs; });
	DynExpr::BindOperator("/", EAssociativity::Left, 5, +[](double lhs, double rhs) { return lhs / rhs; });
	DynExpr::BindOperator("**", EAssociativity::Right, 7, +[](double lhs, double rhs) { return std::pow(lhs, rhs); });

	DynExpr::BindOperator("+", EAssociativity::Left, 3, +[](Vector lhs, Vector rhs) { return lhs + rhs; });
	DynExpr::BindOperator("-", EAssociativity::Left, 3, +[](Vector lhs, Vector rhs) { return lhs - rhs; });
	DynExpr::BindOperator("*", EAssociativity::Left, 5, +[](Vector lhs, double rhs) { return lhs * rhs; });
	DynExpr::BindOperator("*", EAssociativity::Left, 5, +[](double lhs, Vector rhs) { return lhs * rhs; });
	DynExpr::BindOperator("/", EAssociativity::Left, 5, +[](Vector lhs, double rhs) { return lhs / rhs; });

	DynExpr::BindFunction("random", &UTIL_Random<double>);

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
