/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/
#include "precomp.h"

PCONSOLE_STATE_INFO gpStateInfo;

LONG gcxScreen;
LONG gcyScreen;

BOOL g_fForceV2;
BOOL g_fEditKeys;
BYTE g_bPreviewOpacity = 0x00;   //sentinel value for initial test on dialog entry. Once initialized, won't be less than TRANSPARENCY_RANGE_MIN

BOOL g_fHostedInFileProperties = FALSE;

UINT OEMCP;
BOOL g_fEastAsianSystem;

const wchar_t g_szPreviewText[] = \
    L"C:\\WINDOWS> dir                       \n" \
    L"SYSTEM       <DIR>     10-01-99   5:00a\n" \
    L"SYSTEM32     <DIR>     10-01-99   5:00a\n" \
    L"README   TXT     26926 10-01-99   5:00a\n" \
    L"WINDOWS  BMP     46080 10-01-99   5:00a\n" \
    L"NOTEPAD  EXE    337232 10-01-99   5:00a\n" \
    L"CLOCK    AVI     39594 10-01-99   5:00p\n" \
    L"WIN      INI      7005 10-01-99   5:00a\n";

BOOL fChangeCodePage = FALSE;

WCHAR DefaultFaceName[LF_FACESIZE];
WCHAR DefaultTTFaceName[LF_FACESIZE];
COORD DefaultFontSize;
BYTE  DefaultFontFamily;
ULONG DefaultFontIndex = 0;
ULONG g_currentFontIndex = 0;

PFONT_INFO FontInfo = NULL;
ULONG NumberOfFonts;
ULONG FontInfoLength;
BOOL gbEnumerateFaces = FALSE;
PFACENODE gpFaceNames = NULL;

BOOL g_fSettingsDlgInitialized = FALSE;

BOOL InEM_UNDO=FALSE;