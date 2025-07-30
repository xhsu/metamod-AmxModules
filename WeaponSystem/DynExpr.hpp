// This should be a module, but due to MSVC ICE it is a header for now.

#pragma once

#pragma region String Util

using u32char = std::conditional_t<sizeof(wchar_t) == sizeof(char32_t), wchar_t, char32_t>;

enum struct CodePoint : uint_fast8_t
{
	WHOLE = 1,
	BEGIN_OF_2 = 2,
	BEGIN_OF_3 = 3,
	BEGIN_OF_4 = 4,
	MID,
	INVALID,
};

static constexpr auto UTIL_CodePointOf(char c) noexcept -> CodePoint
{
	auto const u = static_cast<uint32_t>(bit_cast<std::make_unsigned_t<decltype(c)>>(c));

	if (u <= 0x7F)
		return CodePoint::WHOLE;

	else if ((u & 0b111'000'00) == 0b110'000'00)
		return CodePoint::BEGIN_OF_2;

	else if ((u & 0b1111'0000) == 0b1110'0000)
		return CodePoint::BEGIN_OF_3;

	else if ((u & 0b11111'000) == 0b11110'000)
		return CodePoint::BEGIN_OF_4;

	else if ((u & 0b11'000000) == 0b10'000000)
		return CodePoint::MID;

	else
		return CodePoint::INVALID;
}

static constexpr auto UTIL_ToFullWidth(std::span<const char> arr) noexcept -> u32char
{
#ifndef _DEBUG
	if (std::ranges::empty(arr))
		return 0x110000;	// Invalid Unicode point, max val is 0x10FFFF
#else
	assert(!std::ranges::empty(arr));
#endif

	std::array const bytes{
		static_cast<uint8_t>(arr[0]),
		arr.size() > 1 ? static_cast<uint8_t>(arr[1]) : (uint8_t)0,
		arr.size() > 2 ? static_cast<uint8_t>(arr[2]) : (uint8_t)0,
		arr.size() > 3 ? static_cast<uint8_t>(arr[3]) : (uint8_t)0,
	};

	switch (UTIL_CodePointOf(bytes.front()))
	{
	case CodePoint::WHOLE:
		return static_cast<u32char>(bytes.front());

	case CodePoint::BEGIN_OF_2:
	{
		u32char ret = (bytes[0] & 0b00011111) << 6 | (bytes[1] & 0b00111111);

		if (ret < (u32char)0x80)		// Not a valid result, Wrong encoding
			ret = 0;					// Out of UTF8 bound, skip data  
		else if (ret > (u32char)0x7FF)	// Not a valid result, Wrong encoding
			ret = 0;					// Out of UTF8 bound, skip data

		return ret;
	}

	case CodePoint::BEGIN_OF_3:
	{
		u32char ret = (bytes[0] & 0b00001111) << 12 | (bytes[1] & 0b00111111) << 6 | (bytes[2] & 0b00111111);

		if (ret < (u32char)0x800)		// Not a valid result, Wrong encoding
			ret = 0;					// Out of UTF8 bound, skip data  
		else if (ret > (u32char)0xFFFF)	// Not a valid result, Wrong encoding
			ret = 0;					// Out of UTF8 bound, skip data  

		return ret;
	}

	case CodePoint::BEGIN_OF_4:
	{
		u32char ret =
			(bytes[0] & 0b00000111) << 18 | (bytes[1] & 0b00111111) << 12 | (bytes[2] & 0b00111111) << 6 | (bytes[3] & 0b00111111);

		if (ret < (u32char)0x10000)			// Not a valid result, Wrong encoding
			ret = 0;						// Out of UTF8 bound, skip data  
		else if (ret > (u32char)0x10FFFF)	// Not a valid result, Wrong encoding 
			ret = 0;						// Out of UTF8 bound, skip data  

		return ret;
	}

	default:
		assert(false);
		std::unreachable();
	}
}

static constexpr auto UTIL_GraphemeAt(string_view s, std::ptrdiff_t pos) noexcept -> string_view
{
	auto const cp = UTIL_CodePointOf(s[pos]);

	switch (cp)
	{
	case CodePoint::WHOLE:
	case CodePoint::BEGIN_OF_2:
	case CodePoint::BEGIN_OF_3:
	case CodePoint::BEGIN_OF_4:
		return string_view{ std::addressof(s[pos]), (size_t)std::to_underlying(cp) };

	default:
		return "";
	}
}

#pragma endregion String Util

#pragma region Generic Parser

struct error_t final
{
	constexpr error_t(error_t const&) noexcept = default;
	constexpr error_t(error_t&&) noexcept = default;
	constexpr ~error_t() noexcept = default;

	constexpr error_t& operator=(error_t const&) noexcept = default;
	constexpr error_t& operator=(error_t&&) noexcept = default;

	constexpr error_t([[maybe_unused]] std::unexpect_t, string_view line, span<string_view const> segs, string errmsg) noexcept
		: m_Text{ line }, m_SegmentsText{ std::from_range, segs }, m_ErrorMsg{ std::move(errmsg) }, m_Underscore(m_Text.size(), ' ')
	{
		SetupUnderscore();

		// Setup this way means the last segment must be wrong.
		Emphasis(m_SegmentsText.size() - 1);

		assert(m_Text.size() == m_Underscore.size());
		assert(m_SegmentsText.size() == m_SegmentsUnderline.size());
	}

	constexpr error_t(string_view line, span<string_view const> segs, string errmsg) noexcept
		: m_Text{ line }, m_SegmentsText{ std::from_range, segs }, m_ErrorMsg{ std::move(errmsg) }, m_Underscore(m_Text.size(), ' ')
	{
		SetupUnderscore();

		assert(m_Text.size() == m_Underscore.size());
		assert(m_SegmentsText.size() == m_SegmentsUnderline.size());
	}

	constexpr void Emphasis(std::ptrdiff_t idx) noexcept
	{
		m_SegmentsUnderline[idx].front() = '^';

		for (auto& c : m_SegmentsUnderline[idx] | std::views::drop(1))
			c = '~';
	}

	constexpr void Underline(std::ptrdiff_t idx, char ch = '~') noexcept
	{
		std::ranges::fill(m_SegmentsUnderline[idx], ch);
	}

	constexpr void ErrorAt(std::ptrdiff_t idx, string what) noexcept
	{
		Emphasis(idx);
		m_ErrorMsg = std::move(what);
	}

	auto ToString(string_view leading = "") const noexcept -> string
	{
		assert(m_Text.size() == m_Underscore.size());

		return std::format(
			"{2}{0}\n{2}{1}",
			m_Text, m_Underscore,
			leading
		);
	}

	auto ToString(size_t iSpaceCount, std::ptrdiff_t line_num) const noexcept -> string
	{
		assert(m_Text.size() == m_Underscore.size());

		return std::format(
			"{0:>{4}} | {1}\n{2} | {3}",
			line_num, m_Text,
			string(iSpaceCount, ' '), m_Underscore,
			iSpaceCount
		);
	}

	constexpr auto GetText() const noexcept -> string_view const& { return m_Text; }
	constexpr auto GetUnderscore() const noexcept -> string const& { return m_Underscore; }
	constexpr auto GetTextSegment(std::ptrdiff_t idx) const noexcept -> string_view { return m_SegmentsText.at(idx); }
	constexpr auto GetUnderscoreSegment(std::ptrdiff_t idx) const noexcept -> string_view { return string_view{ m_SegmentsUnderline[idx].data(), m_SegmentsUnderline[idx].size() }; }
	constexpr auto GetSegmentCount() const noexcept -> std::ptrdiff_t { assert(m_SegmentsText.size() == m_SegmentsUnderline.size()); return std::ranges::ssize(m_SegmentsText); }

	string m_ErrorMsg{};

private:

	// This function was for setting up 'm_SegmentsUnderline'
	// Assumed that m_Text, m_SegmentsText and m_Underscore were setup correctly.
	constexpr void SetupUnderscore() noexcept
	{
		auto const abs_begin = std::addressof(m_Text[0]);

		for (auto&& seg : m_SegmentsText)
		{
			auto const seg_first = std::addressof(seg[0]) - abs_begin;
			auto const seg_length = seg.length();

			m_SegmentsUnderline.emplace_back(span{ std::addressof(m_Underscore[seg_first]), seg_length });
		}

		assert(m_Text.size() == m_Underscore.size());
		assert(m_SegmentsText.size() == m_SegmentsUnderline.size());
	}

	string_view m_Text{};
	string m_Underscore{};
	vector<string_view> m_SegmentsText{};
	vector<span<char>> m_SegmentsUnderline{};
};

template <
	auto fnIsIdentifier,	/* Anything that considered as a single token is a cell. SIG: bool (*)(string_view token, bool allow_sign); */
	auto fnIsParenthesis,	/* Is the string considered as parenthesizes? SIG: bool (*)(string_view token) */
	auto fnIsOperator		/* Is the string considered as an operator? SIG: bool (*)(string_view token) */
>
constexpr auto Tokenizer(string_view s, string_view separators = " \t\f\v\r\n") noexcept -> expected<vector<string_view>, error_t>
{
	// 1. Parse the string as long as possible, like pre-c++11
	// 2. Kicks off the last character then check again.

	vector<string_view> ret{};
	ret.reserve(s.size());

	bool bAllowSignOnNext = true;	// Should not being reset inter-tokens
	for (size_t pos = 0; pos < s.size(); /* Does nothing */)
	{
		auto len = s.size() - pos;
		while (len > 0)
		{
			auto const token = s.substr(pos, len);
			auto const IsIdentifier = fnIsIdentifier(token, bAllowSignOnNext);	// Function must be a valid identifier itself first. Hence no need to add IsFunction() here.
			auto const bIsOperator = fnIsOperator(token) || fnIsParenthesis(token) || token == ",";	// Must leave comma to SYA for separation.

			if (IsIdentifier || bIsOperator)
			{
				ret.emplace_back(token);
				bAllowSignOnNext = bIsOperator;	// If it is an operator prev, then a sign is allow. Things like: x ^ -2 (x to the power of neg 2)
				break;
			}
			else if (len == 1 && separators.contains(s[pos]))
				break;	// space gets skipped without considered as token.

			--len;
		}

		if (!len)
		{
			// The segment was problematically.
			// But for the sake of the error reporting module, pack the rest part into one piece.
			ret.emplace_back(s.substr(pos));

			// No const here because we need to move it. (RVO)
			error_t err{
				std::unexpect,	// overload selection
				s,
				std::move(ret),
				std::format("Tokenizer error: Unrecognized symbol '{}' found at pos {}", UTIL_GraphemeAt(s, pos), pos),
			};

			return std::unexpected(std::move(err));
		}
		else
			// If parsed, something must be inserted.
			pos += len;
	}

	return std::move(ret);	// Move into expected<>
}

struct op_context_t final
{
	string_view m_Prev{};
	string_view m_Op{};
	string_view m_Next{};
	ptrdiff_t m_Position{ -1 };
};

struct token_t final
{
	// This struct is created because I found it difficult to perform overload resolution
	// if the operand count was lost during the SYA phase.
	// For example, during type-only overload resolution, 'a-b' and '-b' will both have a perfect match,
	// provided there are 2 arguments pushed onto the stack.

	string_view m_Symbol{};
	optional<uint8_t> m_OperandCount{};	// Doesn't exist means not operator.
};

template <
	auto fnIsOperand,			/* Anything that considered as a single token is a cell SIG: bool (*)(string_view token) */
	auto fnIsOperator,			/* Is the string considered as an operator? Remember in SYA, parenthesizes aren't operators. SIG: bool (*)(string_view token) */	
	auto fnIsFunction,			/* Is this cell actually an function? SIG: bool (*)(string_view token) */
	auto fnIsLeftAssociative,	/* Is the operator left associative? SIG: bool (*)(op_context_t const& OpContext, span<string_view const> tokens) */
	auto fnGetPrecedence,		/* Get associativity of an operator. SIG: int (*)(op_context_t const& OpContext, span<string_view const> tokens) */
	auto fnGetOperandCount		/* How many operand does this operator needed? SIG: optional<uint8_t> (*)(op_context_t const& OpContext, span<string_view const> tokens) */
>
constexpr auto ShuntingYardAlgorithm(span<string_view const> tokens) noexcept -> expected<vector<token_t>, string>
{
	vector<token_t> ret{};
	vector<op_context_t> op_stack{};

	for (ptrdiff_t i = 0; i < std::ssize(tokens); ++i)
	{
		auto& token = tokens[i];

		bool const bIsOperand = fnIsOperand(token);
		bool const bIsFunction = fnIsFunction(token);
		bool const bIsOperator = fnIsOperator(token);

		// Is number?
		if (bIsOperand && !bIsFunction && !bIsOperator)
		{
			ret.push_back({ token, nullopt });
		}

		// Is a function?
		else if (bIsFunction && !bIsOperator)
		{
			// Function doesn't need context like operator does.
			op_stack.push_back({ "", token, "", i });
		}

		// operator? Remember that parenthesis is not an operator.
		else if (bIsOperator)
		{
			op_context_t OpContext {
				.m_Prev{ i == 0 ? "" : tokens[i - 1] },
				.m_Op{ tokens[i]},
				.m_Next{ i == (std::ssize(tokens) - 1) ? "" : tokens[i + 1]},
				.m_Position = i,
			};

			auto const o1_preced = *fnGetPrecedence(OpContext, tokens);

			/*
			while (
				there is an operator o2 at the top of the operator stack which is not a left parenthesis,
				and (o2 has greater precedence than o1 or (o1 and o2 have the same precedence and o1 is left-associative))
			):
				pop o2 from the operator stack into the output queue
			push o1 onto the operator stack
			*/

			while (!op_stack.empty() && op_stack.back().m_Op != "("
				&& (*fnGetPrecedence(op_stack.back(), tokens) > o1_preced || (*fnGetPrecedence(op_stack.back(), tokens) == o1_preced && *fnIsLeftAssociative(OpContext, tokens)))
				)
			{
				ret.push_back({ op_stack.back().m_Op, fnGetOperandCount(op_stack.back(), tokens) });
				op_stack.pop_back();
			}

			op_stack.push_back(OpContext);
		}

		// (
		else if (token.length() == 1 && token[0] == '(')
			op_stack.push_back({ "", "(", "", i });

		// )
		else if (token.length() == 1 && token[0] == ')')
		{
			try
			{
				while (op_stack.back().m_Op != "(")
				{
					// { assert the operator stack is not empty }
					assert(!op_stack.empty());

					// pop the operator from the operator stack into the output queue

					ret.push_back({ op_stack.back().m_Op, fnGetOperandCount(op_stack.back(), tokens) });
					op_stack.pop_back();
				}

#ifdef _DEBUG
				assert(op_stack.back().m_Op[0] == '(');
#else
				if (op_stack.back().m_Op[0] != '(') [[unlikely]]
					return std::unexpected("SYA error: Top of the stack is not a '(' - assertion failed");
#endif
				// pop the left parenthesis from the operator stack and discard it
				op_stack.pop_back();
			}
			catch (...)
			{
				/* If the stack runs out without finding a left parenthesis, then there are mismatched parentheses. */
				return std::unexpected("SYA error: Mismatched parentheses");
			}

			/*
			if there is a function token at the top of the operator stack, then:
				pop the function from the operator stack into the output queue
			*/
			if (!op_stack.empty() && fnIsFunction(op_stack.back().m_Op))
			{
				ret.push_back({ op_stack.back().m_Op, nullopt });
				op_stack.pop_back();
			}
		}

		// Ignore comma, it's only a separator for function call.
		// The reason why we are leaving it here is for the sake of things like random(-5, -7)
		// Tossing the comma out early will lead to some bad interpretation, like random(-5 -7)
		// The effect of ',' is to force the eval of previous argument.
		// Otherwise, expr like -sin(0) will be very wrong, the negate op will be postponed to the end.
		else if (token.length() == 1 && token[0] == ',')
		{
			while (!op_stack.empty() && op_stack.back().m_Op != "(")
			{
				ret.push_back({ op_stack.back().m_Op, fnGetOperandCount(op_stack.back(), tokens) });
				op_stack.pop_back();
			}
		}

		else
			return std::unexpected(std::format("SYA error: Unreconsized symbol '{}'", token));
	}

	/* After the while loop, pop the remaining items from the operator stack into the output queue. */
	while (!op_stack.empty())
	{
		if (op_stack.back().m_Op == "(")
			return std::unexpected("SYA error: If the operator token on the top of the stack is a parenthesis, then there are mismatched parentheses");

		ret.push_back({ op_stack.back().m_Op, fnGetOperandCount(op_stack.back(), tokens) });
		op_stack.pop_back();
	}

	return std::move(ret);	// For constructing expected<> object
}

#pragma endregion Generic Parser

enum EBuiltInOpPrecedence : uint8_t
{
	// https://en.cppreference.com/w/cpp/language/operator_precedence.html

	// #1, L
	OpPrec_ScopeResolution = 255,	// a::b

	// #2, L
	OpPrec_MemberAccess = 254,	// a.b	a->b
	OpPrec_FunctionCall = 253,	// a()
	OpPrec_SuffixIncDec = 252,	// a++	a--

	// #3, R
	OpPrec_PrefixIncDec = 251,	// ++a	--a
	OpPrec_Unary = 251,			// +a	-a	!a	~a
	OpPrec_TypeCast = 251,		// (type)a
	OpPrec_Indirection = 251,	// *a
	OpPrec_AddressOf = 251,		// &a
	OpPrec_SizeOf = 251,		// sizeof a
	OpPrec_Await = 251,			// co_await a
	OpPrec_HeapAlloc = 251,		// new a	new a[b]	delete a	delete[] a

	// #4, L
	OpPrec_PointToMember = 243,	// a.*b		a->*b

	// #5, L
	OpPrec_Multiplication = 242,	// a * b	a / b	a % b

	// #6, L
	OpPrec_Addition = 241,			// a + b	a - b

	// #7, L
	OpPrec_BitShift = 240,			// a << b	a >> b

	// #8, L
	OpPrec_ThreewayCmp = 239,		// a <=> b

	// #9, L
	OpPrec_Relational = 238,		// a < b	a <= b	a > b	a >= b

	// #10, L
	OpPrec_Equality = 237,			// a == b	a != b

	// #11, L
	OpPrec_BitAND = 236,			// a & b

	// #12, L
	OpPrec_BitXOR = 235,			// a ^ b

	// #13, L
	OpPrec_BitOR = 234,				// a | b

	// #14, L
	OpPrec_LogicalAND = 233,		// a && b

	// #15, L
	OpPrec_LogicalOR = 232,			// a || b

	// #16, R
	OpPrec_TernaryConditional = 231,	// a ? b : c
	OpPrec_Throw = 230,
	OpPrec_Yield = 229,
	OpPrec_Assignment = 228,	// a = b   a += b   a -= b   a *= b   a /= b   a %= b   a <<= b   a >>= b   a &= b   a ^= b   a |= b

	// #17, L
	OpPrec_Comma = 1,
};

enum struct EAssociativity : bool
{
	Right = false,
	Left = true,
};

namespace DynExpr
{
	struct Function final
	{
		move_only_function<any(span<any>) const> m_callable{};	// #UPDATE_AT_CPP26 copyable_function
		ptrdiff_t m_iParamCount{};
		vector<type_info const*> m_ParamTypes{};
		EAssociativity m_Associativity{ EAssociativity::Left };
		uint8_t m_OpPrecedence{ OpPrec_FunctionCall };
	};

	inline map<string_view, vector<Function>, std::ranges::less> m_Functions{};
	inline vector<any> m_Stack{};

	template <typename R, typename... Params>
	auto BindFunction(string_view szName, R(*pfn)(Params...)) noexcept -> Function&
	{
		return m_Functions[szName].emplace_back(
			[pfn](span<any> params) -> any
			{
				return[&]<size_t... I>(std::index_sequence<I...>) -> any
				{
					if constexpr (std::same_as<R, void>)
					{
						pfn(any_cast<Params&&>(std::move(params[I]))...);
						return {};
					}
					else
						return pfn(any_cast<Params&&>(std::move(params[I]))...);
				}
				(std::index_sequence_for<Params...>{});
			},
			(ptrdiff_t)sizeof...(Params),
			decltype(Function::m_ParamTypes){ &typeid(Params)... }	// Explict template argument to support function with 0 arg.
		);
	}

	template <typename R, typename... Params>
	void BindOperator(string_view szName, EAssociativity Associativity, uint8_t OpPrec, R(*pfn)(Params...)) noexcept
	{
		if (szName == ",") [[unlikely]]
		{
			assert(false);
#ifndef _DEBUG
			// One must NOT use comma as an operator!
			std::abort();
#endif
		}

		auto& Result = BindFunction(szName, pfn);
		Result.m_Associativity = Associativity;
		Result.m_OpPrecedence = OpPrec;

		for (auto&& FnBinder : m_Functions[szName])
		{
			if (FnBinder.m_iParamCount == Result.m_iParamCount &&
				(FnBinder.m_Associativity != Associativity || FnBinder.m_OpPrecedence != OpPrec)) [[unlikely]]
			{
				assert(false);
#ifndef _DEBUG
				// All operator and function should have exactly same prec and assoc!
				std::abort();
#endif
			}
		}

		auto& OverloadSet = m_Functions[szName];
		std::ranges::stable_sort(OverloadSet, std::ranges::greater{}, &Function::m_OpPrecedence);
	}

	inline map<string_view, any, std::ranges::less> m_Constants{
		{ u8"e",	std::numbers::e		},
		{ u8"ϕ",	std::numbers::phi	},
		{ u8"phi",	std::numbers::phi	},
		{ u8"π",	std::numbers::pi	},
		{ u8"pi",	std::numbers::pi	},

		// SI defining constants

		{ u8"h",		6.62607015e-34	},	// Planck constant (ℎ/ℏ)
		{ u8"e[0]",		1.602176634e-19	},	// Elementary charge
		{ u8"k[B]",		1.380649e-23	},	// Boltzmann constant
		{ u8"N[A]",		6.022'14076e23	},	// Avogadro constant
		{ u8"c",		299'792'458		},	// Speed of light in vacuum
		{ u8"ΔνCs",		9.192'631'770e9	},	// Unperturbed ground-state hyperfine transition frequency of the cesium-133 atom
		{ u8"K[cd]",	683				},	// Luminous efficacy of monochromatic radiation of frequency 540×10^12 hertz

		// Faraday constant			F
		// Gravitational constant	G
		// Conductance quantum		G[0]
		// Molar gas constant		R
		// Electron volt			eV
		// Standard gravity			g
		// Electron mass			m[e]
		// Proton mass				m[p]
		// Atomic mass constant		m[u]
		// Electric constant		ε[0]
		// Magnetic constant		μ[0]
	};

	template <typename T>
	void BindConstant(string_view szName, auto&&... args) noexcept
	{
		if constexpr (std::same_as<T, void> || sizeof...(args) == 0)
			m_Constants.erase(szName);
		else
			m_Constants[szName].emplace<T>(std::forward<decltype(args)>(args)...);
	}

	struct ClassMem final
	{
		any m_PointerToMem{};
		type_info const* m_ClassType{ &typeid(void) };
		type_info const* m_ClassPointerType{ &typeid(void) };
	};

	inline map<string_view, vector<ClassMem>, std::ranges::less> m_ClassMembers{};

	template <typename T, class C>
	void BindMember(string_view szName, T C::* pMem) noexcept
	{
		m_ClassMembers[szName].emplace_back(
			any{ std::in_place_type<decltype(pMem)>, pMem },
			&typeid(C),
			&typeid(C*)
		);

		// Avoid repeat register
		static map<type_info const*, set<type_info const*, std::ranges::less>, std::ranges::less>
			m_SupportedClasses{};

		// Automatically list '*' as deref operator for this class.
		// Must put before '.' and '->' because they are going to create the class map.
		if (!m_SupportedClasses.contains(&typeid(C)))
		{
			BindOperator(
				"*", EAssociativity::Right, OpPrec_Indirection,
				+[](C* object) noexcept -> C { return std::move(*object); }
			);
		}

		// Automatically list '.' as member accessing operator for this class.
		if (!m_SupportedClasses[&typeid(C)].contains(&typeid(T)))
		{
			BindOperator(
				".", EAssociativity::Left, OpPrec_MemberAccess,
				// Normalize all arithmetic types into double
				+[](C object, T C::* ptm) noexcept -> std::conditional_t<std::is_arithmetic_v<T>, double, T> { return std::invoke(ptm, object); }
			);
			BindOperator(
				"->", EAssociativity::Left, OpPrec_MemberAccess,
				// Normalize all arithmetic types into double
				+[](C* object, T C::* ptm) noexcept -> std::conditional_t<std::is_arithmetic_v<T>, double, T> { return std::invoke(ptm, *object); }
			);
			m_SupportedClasses[&typeid(C)].emplace(&typeid(T));
		}
	}

	template <class C, typename R, typename... Params>
	auto BindMethod(string_view szName, R(C::* pfn)(Params...) const) noexcept -> Function&
	{
		m_Functions[szName].push_back(
			Function {
				[pfn](span<any> args) -> any
				{
					return[&]<size_t... I>(std::index_sequence<I...>) -> any
					{
						if constexpr (std::same_as<R, void>)
						{
							std::invoke(pfn, any_cast<C&&>(std::move(args[0])), any_cast<Params&&>(std::move(args[1 + I]))...);
							return {};
						}
						else
							return std::invoke(pfn, any_cast<C&&>(std::move(args[0])), any_cast<Params&&>(std::move(args[1 + I]))...);
					}
					(std::index_sequence_for<Params...>{});
				},
				(ptrdiff_t)sizeof...(Params) + 1,
				decltype(Function::m_ParamTypes){ &typeid(C), &typeid(Params)... }
			}
		);

		return m_Functions[szName].back();
	}

	void Call(token_t token)
	{
		bool bCalled = false;
		string szErrMsg{};

		auto&& overloads = m_Functions.at(token.m_Symbol);
		for (auto&& func : overloads)
		{
			// Filter by operand, if it's an operator.
			if (token.m_OperandCount)
			{
				if (token.m_OperandCount != func.m_iParamCount)
				{
					szErrMsg =
						std::format("Operator resolution failed: expected {:x} operand(s) but {} received.", *token.m_OperandCount, func.m_iParamCount);
					continue;
				}
			}

			auto&& bgn = std::ranges::prev(
				m_Stack.end(),
				std::ranges::min(std::ssize(m_Stack), func.m_iParamCount)
			);
			span const args{ bgn, m_Stack.end() };

			if (std::ssize(args) != func.m_iParamCount)
			{
				szErrMsg = std::format("Function '{}' expecting {} parameters, but {} arguments received.", token.m_Symbol, func.m_iParamCount, args.size());
				continue;
			}

			bool bResolved = true;
			for (int i{}; auto&& [arg, param] : std::views::zip(args, func.m_ParamTypes))
			{
				if (type_index(arg.type()) != *param)
				{
					szErrMsg = std::format("Type of argument #{} ('{}') mismatch with its parameter type ('{}')", i, arg.type().name(), param->name());
					bResolved = false;
					break;
				}

				++i;
			}

			if (!bResolved)
				continue;

			bCalled = true;

			auto&& res = func.m_callable(args);
			m_Stack.erase(bgn, m_Stack.end());

			if (res.has_value())
				m_Stack.emplace_back(std::move(res));

			break;
		}

		if (!bCalled)
			throw std::runtime_error{ szErrMsg };
	}

	template <typename T>
	decltype(auto) Push(auto&&... what)
	{
		return m_Stack.emplace_back(
			std::in_place_type<T>,
			std::forward<decltype(what)>(what)...
		);
	}

	bool TryPushConst(string_view szName) noexcept
	{
		if (auto const it = m_Constants.find(szName); it != m_Constants.cend())
		{
			m_Stack.emplace_back(it->second);
			return true;
		}

		return false;
	}

	bool TryPushMem(string_view szName) noexcept
	{
		// There could be multiple member named the same.
		if (auto const it = m_ClassMembers.find(szName);
			it != m_ClassMembers.cend() && !m_Stack.empty())
		{
			auto const& ClassMems = it->second;
			for (auto&& MemInfo : ClassMems)
			{
				// The object must match the type from member info. 'cause it's a ptr to class.
				if (m_Stack.back().type() != *MemInfo.m_ClassType
					&& m_Stack.back().type() != *MemInfo.m_ClassPointerType)
					continue;

				m_Stack.push_back(MemInfo.m_PointerToMem);
				return true;
			}

			return false;
		}

		return false;
	}

	auto PushLiteral(string_view s) noexcept -> expected<any*, string>
	{
		if (s.empty())
			return std::unexpected("Empty token");

		// String literal
		if (s.front() == '"' && s.back() == '"' && s.size() >= 2)
			return &Push<string_view>(s.substr(1, s.size() - 2));
		if (s.front() == '\'' && s.back() == '\'')
		{
			if (s.size() == 2)
				return std::unexpected("Empty character ref '' encountered");

			auto const CP = UTIL_CodePointOf(s[1]);
			switch (CP)
			{
			case CodePoint::WHOLE:
			case CodePoint::BEGIN_OF_2:
			case CodePoint::BEGIN_OF_3:
			case CodePoint::BEGIN_OF_4:
				if (s.size() != std::to_underlying(CP) + 2)
					return std::unexpected(std::format("Bad UTF-8 encoding. {} bytes expected but {} found.", std::to_underlying(CP), s.size() - 2));
				
				return &Push<u32char>(UTIL_ToFullWidth({ &s[1], std::to_underlying(CP) }));

			default:
				return std::unexpected("Corrupted UTF-8 encoding.");
			}
			std::unreachable();
		}

		// Arithmetic

		int base = 10;

		if (s.starts_with("0x") || s.starts_with("0X"))
			base = 16;
		if (s.starts_with("0o") || s.starts_with("0O"))
			base = 8;
		if (s.starts_with("0b") || s.starts_with("0B"))
			base = 2;

		if (base != 10)
		{
			uint32_t ret{};	// Plus 2 to skip the 0* part
			if (std::from_chars(s.data() + 2, s.data() + s.size(), ret, base).ec == std::errc{})
				return &Push<double>((double)ret);

			return std::unexpected(std::format("Fail to interpret Base{} number '{}'", base, s));
		}

		double ret{};
		if (std::from_chars(s.data(), s.data() + s.size(), ret).ec == std::errc{})
			return &Push<double>(ret);

		return std::unexpected(std::format("Fail to interpret assumed-number '{}'", s));
	}

	template <typename T>
	T Pop() noexcept
	{
		auto out = std::move(m_Stack.back());
		m_Stack.pop_back();

		if constexpr (std::same_as<T, any>)
			return std::move(out);
		else
			return any_cast<T&&>(std::move(out));
	}

	//

	constexpr bool IsIdentifier(string_view s) noexcept
	{
		if (s.empty())
			return false;

		bool const legit_starting =
			('a' <= s.front() && s.front() <= 'z')
			or ('A' <= s.front() && s.front() <= 'Z')
			or s.front() == '_'
			or bit_cast<uint8_t>(s.front()) & (uint8_t)(0b1000'0000)
			;

		if (!legit_starting)
			return false;

		for (auto c : s | std::views::drop(1))
		{
			// Same rule in C/C++
			bool const legit =
				('0' <= c && c <= '9')
				or ('a' <= c && c <= 'z')
				or ('A' <= c && c <= 'Z')
				or c == '_'
				or bit_cast<uint8_t>(c) & (uint8_t)(0b1000'0000)
				;

			if (!legit)
				return false;
		}

		return true;
	}

	constexpr bool IsLiteral(string_view s, bool const bAllowSign = true) noexcept
	{
		if (s.empty())
			return false;

		// String literal.
		if (s.front() == '"' && s.back() == '"' && s.size() >= 2)
			return true;
		if (s.front() == '\'' && s.back() == '\'' && s.size() >= 2)
			return true;

		auto const bSigned = (s.front() == '-' || s.front() == '+') && bAllowSign;
		// Kick the sign off, it's really messing things up.
		if (bSigned)
			s = s.substr(1);

		if (s.empty())	// What? only a sign was passed in?
			return false;

		// Is IRL constant?
		if not consteval
		{
			if (m_Constants.contains(s))
				return true;
		}

		bool const bHex = s.starts_with("0x") || s.starts_with("0X");
		bool const bOct = s.starts_with("0o") || s.starts_with("0O");
		bool const bBin = s.starts_with("0b") || s.starts_with("0B");
		auto const bindig_count = std::ranges::count_if(s, [](char c) static noexcept { return '0' <= c && c <= '1'; });
		auto const octdig_count = std::ranges::count_if(s, [](char c) static noexcept { return '0' <= c && c <= '7'; });
		auto const decdig_count = std::ranges::count_if(s, [](char c) static noexcept { return '0' <= c && c <= '9'; });
		auto const hexdig_count = std::ranges::count_if(s, [](char c) static noexcept { return "0123456789ABCDEFabcdef"sv.contains(c); });
		auto const dot_count = std::ranges::count(s, '.');
		auto const e_count = std::ranges::count(s, 'e') + std::ranges::count(s, 'E');
		auto const sign_count = std::ranges::count(s, '+') + std::ranges::count(s, '-');	// this sign is not for overall sign, it's the sign of power -- like in '1e-9'

		// It must be starting from 0-9 even if you are doing hex, as it starts as '0x'
		bool const bIsFrontDigit = '0' <= s.front() && s.front() <= '9';
		bool const bIsBackDigit = '0' <= s.back() && s.back() <= '9';

		// Filter out some obvious error.
		if (!bIsFrontDigit || dot_count > 1 || sign_count > 1)
			return false;	// Can have only one dot.

		// Integral literal.
		if (bBin && bindig_count == (std::ssize(s) - 1))
			return true;
		if (bOct && octdig_count == (std::ssize(s) - 1))
			return true;
		if (decdig_count == std::ssize(s))
			return true;
		if (bHex && hexdig_count == (std::ssize(s) - 1))
			return true;

		// Floating point literal.
		if ((e_count == 1 || dot_count == 1) && decdig_count == (std::ssize(s) - dot_count - e_count - sign_count) && bIsBackDigit)
			return true;	// floating point number must not be hex.

		return false;
	}

	static_assert(IsLiteral("1234") && IsLiteral("1e8"));
	static_assert(IsLiteral("0xABCD"));
	static_assert(!IsLiteral("0o5678"));	// Bad: oct number containing '8'
	static_assert(!IsLiteral("0.1.1"));	// Bad: Version number.
	static_assert(IsLiteral("-12.34e-5"));
	static_assert(!IsLiteral("--12.34e-5"));	// Bad: too many signs
	static_assert(!IsLiteral("-12.34ef5"));	// Bad: floating with 'f'
	static_assert(!IsLiteral("1.") && !IsLiteral("1e"));	// Bad: Bad fp format.

	constexpr bool IsOpenBracket(string_view s) noexcept
	{
		return s == "(";
	}

	constexpr bool IsCloseBracket(string_view s) noexcept
	{
		return s == ")";
	}

	bool IsFunction(string_view s) noexcept	// Will exclude operators.
	{
		if (auto const it = m_Functions.find(s); it != m_Functions.cend())
		{
			for (auto&& FnBinder : it->second)
			{
				if (FnBinder.m_OpPrecedence != OpPrec_FunctionCall)
					return false;
			}

			return true;
		}

		return false;
	}

	bool IsOperator(string_view s) noexcept
	{
		if (auto const it = m_Functions.find(s); it != m_Functions.cend())
		{
			for (auto&& FnBinder : it->second)
			{
				if (FnBinder.m_OpPrecedence == OpPrec_FunctionCall)
					return false;
			}

			return true;
		}

		return false;
	}

	optional<uint8_t> GetOperandCount(op_context_t const& OpContext, span<string_view const> tokens) noexcept
	{
		if (!IsOperator(OpContext.m_Op))
			return std::nullopt;

		uint8_t ret{ 0 };

		/*
		FN() * a
		CONST * a
		*/

		if (IsCloseBracket(OpContext.m_Prev) || IsIdentifier(OpContext.m_Prev) || IsLiteral(OpContext.m_Prev))
			++ret;

		/*
		a * (b + c)
		a * CONST
		a * FN()
		*/

		if (IsOpenBracket(OpContext.m_Next) || IsIdentifier(OpContext.m_Next) || IsLiteral(OpContext.m_Next)
			|| IsFunction(OpContext.m_Next))	// IsFunction() MUST EXCLUDE operators!
		{
			++ret;
		}

		/*    |
		CONST * -*a	// CONST multiply negated deref 'a'
		CONST / -(a + b)
		*/
		if (ret < 2)
		{
			ptrdiff_t iUnaryDebt = 0, i = OpContext.m_Position + 1;
			for (; i < std::ssize(tokens); ++i)
			{
				if (IsOperator(tokens[i]))
				{
					auto const& OverloadSet = m_Functions[tokens[i]];
					for (auto&& Candidate : OverloadSet)
					{
						// Has a potential to function as unary op is enough.
						if (Candidate.m_iParamCount == 1 && Candidate.m_Associativity == EAssociativity::Right)
						{
							++iUnaryDebt;
							break;
						}
					}
				}
				else if (IsOpenBracket(tokens[i]) || IsIdentifier(tokens[i]) || IsLiteral(tokens[i]) || IsFunction(tokens[i]))
					// Stop searching once a operand is found.
					break;
			}

			// iUnaryDebt == 0 means the next token is already an operand - will count twice if we ++ here.
			// Every step between this operator and the next operand is right-acc unary op. Good enough.
			if (iUnaryDebt != 0 && iUnaryDebt == (i - (OpContext.m_Position + 1)))
				++ret;
		}

		/*
		a++ + CONST
		(a->b)++ + CONST
		*/
		if (ret < 2)
		{
			ptrdiff_t iUnaryDebt = 0, i = OpContext.m_Position - 1;
			for (; i > -1; --i)
			{
				if (IsOperator(tokens[i]))
				{
					auto const& OverloadSet = m_Functions[tokens[i]];
					for (auto&& Candidate : OverloadSet)
					{
						// Has a potential to function as unary op is enough.
						if (Candidate.m_iParamCount == 1 && Candidate.m_Associativity == EAssociativity::Left)
						{
							++iUnaryDebt;
							break;
						}
					}
				}
				else if (IsCloseBracket(tokens[i]) || IsIdentifier(tokens[i]) || IsLiteral(tokens[i]))
					// Stop searching once a operand is found.
					break;
			}

			// Every step between this operator and the next operand is right-acc unary op. Good enough.
			if (iUnaryDebt != 0 && iUnaryDebt == (OpContext.m_Position + 1 - i))
				++ret;
		}

		return ret;
	}

	optional<bool> IsLeftAssociative(op_context_t const& OpContext, span<string_view const> tokens) noexcept
	{
		auto const iOperandCount = GetOperandCount(OpContext, tokens);

		if (auto const it = m_Functions.find(OpContext.m_Op); it != m_Functions.cend())
		{
			// Assume the overload set is sorted by precedence.
			auto const& OverloadSet = it->second;

			for (auto&& Candidate : OverloadSet)
			{
				if (Candidate.m_iParamCount == iOperandCount)
					return Candidate.m_Associativity == EAssociativity::Left;
			}
		}

		//throw std::runtime_error{ "Not an operator!" };
		return std::nullopt;
	}

	optional<uint8_t> GetOpPrecedence(op_context_t const& OpContext, span<string_view const> tokens) noexcept
	{
		auto const iOperandCount = GetOperandCount(OpContext, tokens);

		if (auto const it = m_Functions.find(OpContext.m_Op); it != m_Functions.cend())
		{
			// Assume the overload set is sorted by precedence.
			auto const& OverloadSet = it->second;

			for (auto&& Candidate : OverloadSet)
			{
				if (Candidate.m_iParamCount == iOperandCount)
					return Candidate.m_OpPrecedence;
			}
		}

		//throw std::runtime_error{ "Not an operator!" };
		return std::nullopt;
	}

	__forceinline auto Tokenizer(string_view const& s) noexcept
	{
		return
			::Tokenizer<
				// Including regular functions but excluding operators
				[](string_view s, bool b) noexcept { return (IsIdentifier(s) && !IsOperator(s)) || IsLiteral(s, b); },
				[](string_view s) noexcept { return IsOpenBracket(s) || IsCloseBracket(s); },
				&IsOperator
			>(s, " \t\f\v\r\n");
	}

	__forceinline auto ShuntingYardAlgorithm(span<string_view const> tokens) noexcept
	{
		return
			::ShuntingYardAlgorithm<
				// Excluding both functions and operators.
				[](auto&& s) noexcept { return (IsIdentifier(s) && !m_Functions.contains(s)) || IsLiteral(s); },
				&IsOperator,
				&IsFunction,
				&IsLeftAssociative,
				&GetOpPrecedence,
				&GetOperandCount
			>(tokens);
	}

	//

	template <typename T>
	auto Execute(span<token_t const> Instructions) noexcept -> expected<T, string>
	{
		m_Stack.clear();

		try
		{
			for (auto&& token : Instructions)
			{
				if (token.m_Symbol == ",") {	// NOP
				}
				else if (TryPushMem(token.m_Symbol)) {
				}
				else if (TryPushConst(token.m_Symbol)) {
				}
				else if (IsLiteral(token.m_Symbol)) {
					if (auto res = PushLiteral(token.m_Symbol); !res)
						throw std::runtime_error{ std::move(res).error() };
				}
				else if (IsFunction(token.m_Symbol) || IsOperator(token.m_Symbol))
					Call(token);
				else
					throw std::runtime_error{ std::format("Unknow token '{}'", token.m_Symbol) };
			}

			if (m_Stack.size() != 1)
				throw std::runtime_error{ std::format("ESP corrpution detected, stack size == {}", m_Stack.size()) };

			return Pop<T>();
		}
		catch (const std::exception& e)
		{
			return std::unexpected(e.what());
		}
	}
}

