#ifdef __INTELLISENSE__
import std;
#else
import std.compat;
#endif
import hlsdk;

import CBase;
import Plugin;

import UtlHook;



static uint8_t* gpBuf{};
static int giSize{};
static int giRead{};
static int giBadRead{};

int READ_OK(void)
{
	return !giBadRead;
}

void BEGIN_READ(void* buf, int size)
{
	giRead = 0;
	giBadRead = 0;
	giSize = size;
	gpBuf = (uint8_t*)buf;
}


int READ_CHAR(void)
{
	int     c;

	if (giRead + 1 > giSize)
	{
		giBadRead = true;
		return -1;
	}

	c = (signed char)gpBuf[giRead];
	giRead++;

	return c;
}

int READ_BYTE(void)
{
	int     c;

	if (giRead + 1 > giSize)
	{
		giBadRead = true;
		return -1;
	}

	c = (unsigned char)gpBuf[giRead];
	giRead++;

	return c;
}

int READ_SHORT(void)
{
	int     c;

	if (giRead + 2 > giSize)
	{
		giBadRead = true;
		return -1;
	}

	c = (short)(gpBuf[giRead] + (gpBuf[giRead + 1] << 8));

	giRead += 2;

	return c;
}

int READ_WORD(void)
{
	return READ_SHORT();
}


int READ_LONG(void)
{
	int     c;

	if (giRead + 4 > giSize)
	{
		giBadRead = true;
		return -1;
	}

	c = gpBuf[giRead] + (gpBuf[giRead + 1] << 8) + (gpBuf[giRead + 2] << 16) + (gpBuf[giRead + 3] << 24);

	giRead += 4;

	return c;
}

float READ_FLOAT(void)
{
	union
	{
		uint8_t    b[4];
		float   f;
		int     l;
	} dat;

	dat.b[0] = gpBuf[giRead];
	dat.b[1] = gpBuf[giRead + 1];
	dat.b[2] = gpBuf[giRead + 2];
	dat.b[3] = gpBuf[giRead + 3];
	giRead += 4;

	//	dat.l = LittleLong (dat.l);

	return dat.f;
}

char* READ_STRING(void)
{
	static char     string[2048];
	size_t		l;
	int             c;

	string[0] = 0;

	l = 0;
	do
	{
		if (giRead + 1 > giSize)
			break; // no more characters

		c = READ_CHAR();
		if (c == -1 || c == 0)
			break;
		string[l] = c;
		l++;
	} while (l < sizeof(string) - 1);

	string[l] = 0;

	return string;
}

float READ_COORD(void)
{
	return (float)(READ_SHORT() * (1.0 / 8));
}

float READ_ANGLE(void)
{
	return (float)(READ_CHAR() * (360.0 / 256));
}

float READ_HIRESANGLE(void)
{
	return (float)(READ_SHORT() * (360.0 / 65536));
}

struct MsgFunc_WeaponList final
{
	static inline constexpr char MODULE[] = "client.dll";
	static inline constexpr char NAME[] = u8"::CHudAmmo::MsgFunc_WeaponList";
	static inline constexpr std::tuple PATTERNS
	{
		std::cref("\xCC\x55\x8B\xEC\x81\xEC****\xA1****\x33\xC5\x89\x45\xFC\x8B\x45\x10\x56\xFF\x75\x0C\x50\xE8"),	// NEW
	};
	static inline constexpr std::ptrdiff_t DISPLACEMENT = 1;
	static inline int(__fastcall* pfn)(void* pThis, void* edx, const char* pszName, int iSize, void* pbuf) noexcept = nullptr;
};

extern int __fastcall HOOK_MsgFunc_WeaponList(void* pThis, void* edx, const char* pszName, int iSize, void* pbuf) noexcept;

FunctionHook Hook_WeaponList{ &HOOK_MsgFunc_WeaponList };

int __fastcall HOOK_MsgFunc_WeaponList(void* pThis, void* edx, const char* pszName, int iSize, void* pbuf) noexcept
{
	BEGIN_READ(pbuf, iSize);

	auto sz = READ_STRING();
	auto b1 = READ_BYTE();
	auto b2 = READ_BYTE();
	auto b3 = READ_BYTE();
	auto b4 = READ_BYTE();
	auto b5 = READ_BYTE();
	auto b6 = READ_BYTE();
	auto b7 = READ_BYTE();
	auto b8 = READ_BYTE();
	auto ok = READ_OK();

	return Hook_WeaponList(pThis, edx, pszName, iSize, pbuf);
}