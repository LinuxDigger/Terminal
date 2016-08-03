/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/

#include "precomp.h"

#include "registry.hpp"

#include "globals.h"

#include "cursor.h"
#include "dbcs.h"
#include "srvinit.h"

#pragma hdrstop

#define SET_FIELD_AND_SIZE(x) FIELD_OFFSET(Settings, (x)), RTL_FIELD_SIZE(Settings, (x))

// Default word delimiters if no others are specified.
const WCHAR gaWordDelimCharsDefault[WORD_DELIM_MAX] = L"\\" L"+!:=/.<>;|&";

Registry::Registry(_In_ Settings* const pSettings) :
    _pSettings(pSettings)
{

}

Registry::~Registry()
{

}

// Routine Description:
// - Reads extended edit keys and related registry information into the global state.
// Arguments:
// - hConsoleKey - The console subkey to use for querying.
// Return Value:
// - <none>
void Registry::GetEditKeys(_In_opt_ HANDLE hConsoleKey) const
{
    NTSTATUS Status;
    HANDLE hCurrentUserKey = nullptr;
    if (hConsoleKey == nullptr)
    {
        Status = RegistrySerialization::s_OpenConsoleKey(&hCurrentUserKey, &hConsoleKey);
        if (!NT_SUCCESS(Status))
        {
            return;
        }
    }

    // determine whether the user wants to allow alt-f4 to close the console (global setting)
    DWORD dwValue;
    Status = RegistrySerialization::s_QueryValue(hConsoleKey, CONSOLE_REGISTRY_ALLOW_ALTF4_CLOSE, sizeof(dwValue), (PBYTE)& dwValue, nullptr);
    if (NT_SUCCESS(Status) && dwValue <= 1)
    {
        g_ciConsoleInformation.SetAltF4CloseAllowed(!!dwValue);
    }

    // get extended edit mode and keys from registry.
    Status = RegistrySerialization::s_QueryValue(hConsoleKey, CONSOLE_REGISTRY_EXTENDEDEDITKEY, sizeof(dwValue), (PBYTE)& dwValue, nullptr);
    if (NT_SUCCESS(Status) && dwValue <= 1)
    {
        ExtKeyDefBuf buf = { 0 };

        g_ciConsoleInformation.SetExtendedEditKey(!!dwValue);

        // Initialize Extended Edit keys.
        InitExtendedEditKeys(nullptr);

        Status = RegistrySerialization::s_QueryValue(hConsoleKey, CONSOLE_REGISTRY_EXTENDEDEDITKEY_CUSTOM, sizeof(buf), (PBYTE)& buf, nullptr);
        if (NT_SUCCESS(Status))
        {
            InitExtendedEditKeys(&buf);
        }
    }
    else
    {
        g_ciConsoleInformation.SetExtendedEditKey(false);
    }

    // Word delimiters
    if (g_ciConsoleInformation.GetExtendedEditKey())
    {
        // If extended edit key is given, provide extended word delimiters by default.
        CopyMemory(gaWordDelimChars, gaWordDelimCharsDefault, sizeof(gaWordDelimChars[0]) * WORD_DELIM_MAX);
    }
    else
    {
        // Otherwise, stick to the original word delimiter.
        gaWordDelimChars[0] = L'\0';
    }

    // Read word delimiters from registry
    WCHAR awchBuffer[64];
    Status = RegistrySerialization::s_QueryValue(hConsoleKey, CONSOLE_REGISTRY_WORD_DELIM, sizeof(awchBuffer), (PBYTE)awchBuffer, nullptr);
    if (NT_SUCCESS(Status))
    {
        // OK, copy it to the word delimiter array.
        #pragma prefast(suppress:26035, "RegistrySerialization::s_QueryValue will properly terminate strings.")
        StringCchCopyW(gaWordDelimChars, WORD_DELIM_MAX, awchBuffer);
        gaWordDelimChars[WORD_DELIM_MAX - 1] = 0;
    }

    if (hCurrentUserKey)
    {
        CloseHandle(hConsoleKey);
        CloseHandle(hCurrentUserKey);
    }
}

void Registry::_LoadMappedProperties(_In_reads_(cPropertyMappings) const RegistrySerialization::RegPropertyMap* const rgPropertyMappings,
                                     _In_ size_t const cPropertyMappings,
                                     _In_ HANDLE const hKey)
{
    // Iterate through properties table and load each setting for common property types
    for (UINT iMapping = 0; iMapping < cPropertyMappings; iMapping++)
    {
        const RegistrySerialization::RegPropertyMap* const pPropMap = &(rgPropertyMappings[iMapping]);

        switch (pPropMap->propertyType)
        {
        case RegistrySerialization::_RegPropertyType::Boolean:
        case RegistrySerialization::_RegPropertyType::Dword:
        case RegistrySerialization::_RegPropertyType::Word:
        case RegistrySerialization::_RegPropertyType::Byte:
        case RegistrySerialization::_RegPropertyType::Coordinate:
        {
            RegistrySerialization::s_LoadRegDword(hKey, pPropMap, _pSettings);
            break;
        }
        case RegistrySerialization::_RegPropertyType::String:
        {
            RegistrySerialization::s_LoadRegString(hKey, pPropMap, _pSettings);
            break;
        }
        }
    }
}

// Routine Description:
// - Read settings that apply to all console instances from the registry.
// Arguments:
// - <none>
// Return Value:
// - <none>
void Registry::LoadGlobalsFromRegistry()
{
    HANDLE hCurrentUserKey;
    HANDLE hConsoleKey;
    NTSTATUS status = RegistrySerialization::s_OpenConsoleKey(&hCurrentUserKey, &hConsoleKey);

    if (NT_SUCCESS(status))
    {
        _LoadMappedProperties(RegistrySerialization::s_GlobalPropMappings, RegistrySerialization::s_GlobalPropMappingsSize, hConsoleKey);

        CloseHandle(hConsoleKey);
        CloseHandle(hCurrentUserKey);
    }
}

// Routine Description:
// - Reads default settings from the registry into the current console state.
// Arguments:
// - <none>
// Return Value:
// - <none>
void Registry::LoadDefaultFromRegistry()
{
    LoadFromRegistry(L"");
}

// Routine Description:
// - Reads settings from the registry into the current console state.
// Arguments:
// - pwszConsoleTitle - Name of the console subkey to open. Empty string for the default console settings.
// Return Value:
// - <none>
void Registry::LoadFromRegistry(_In_ PCWSTR const pwszConsoleTitle)
{
    HANDLE hCurrentUserKey;
    HANDLE hConsoleKey;
    NTSTATUS Status = RegistrySerialization::s_OpenConsoleKey(&hCurrentUserKey, &hConsoleKey);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    // Open the console title subkey.
    LPWSTR TranslatedConsoleTitle = TranslateConsoleTitle(pwszConsoleTitle, TRUE, TRUE);
    if (TranslatedConsoleTitle == nullptr)
    {
        CloseHandle(hConsoleKey);
        CloseHandle(hCurrentUserKey);
        return;
    }

    HANDLE hTitleKey;
    Status = RegistrySerialization::s_OpenKey(hConsoleKey, TranslatedConsoleTitle, &hTitleKey);
    ConsoleHeapFree(TranslatedConsoleTitle);
    TranslatedConsoleTitle = nullptr;

    if (!NT_SUCCESS(Status))
    {
        TranslatedConsoleTitle = TranslateConsoleTitle(pwszConsoleTitle, TRUE, FALSE);

        if (TranslatedConsoleTitle == nullptr)
        {
            CloseHandle(hConsoleKey);
            CloseHandle(hCurrentUserKey);
            return;
        }

        Status = RegistrySerialization::s_OpenKey(hConsoleKey, TranslatedConsoleTitle, &hTitleKey);
        ConsoleHeapFree(TranslatedConsoleTitle);
        TranslatedConsoleTitle = nullptr;
    }

    if (!NT_SUCCESS(Status))
    {
        CloseHandle(hConsoleKey);
        CloseHandle(hCurrentUserKey);
        return;
    }

    // Iterate through properties table and load each setting for common property types
    _LoadMappedProperties(RegistrySerialization::s_PropertyMappings, RegistrySerialization::s_PropertyMappingsSize, hTitleKey);

    // Now load complex properties
    // Some properties shouldn't be filled by the registry if a copy already exists from the process start information.
    DWORD dwValue;

    // Window Origin Autopositioning Setting
    Status = RegistrySerialization::s_QueryValue(hTitleKey, CONSOLE_REGISTRY_WINDOWPOS, sizeof(dwValue), (PBYTE)&dwValue, nullptr);

    if (NT_SUCCESS(Status))
    {
        // The presence of a position key means autopositioning is false.
        _pSettings->SetAutoPosition(FALSE);
    }
    //  The absence of the window position key means that autopositioning is true,
    //      HOWEVER, the defaults might not have been auto-pos, so don't assume that they are.

    // Code Page
    Status = RegistrySerialization::s_QueryValue(hTitleKey, CONSOLE_REGISTRY_CODEPAGE, sizeof(dwValue), (PBYTE)& dwValue, nullptr);
    if (NT_SUCCESS(Status))
    {
        _pSettings->SetCodePage(dwValue);

        // If this routine specified default settings for console property,
        // then make sure code page value when East Asian environment.
        // If code page value does not the same to OEMCP and any EA's code page then
        // we are override code page value to OEMCP on default console property.
        // Because, East Asian environment has limitation that doesn not switch to
        // another EA's code page by the SetConsoleCP/SetConsoleOutputCP.
        //
        // Compare of pwszConsoleTitle and L"" has limit to default property of console.
        // It means, this code doesn't care user defined property.
        // Content of user defined property has responsibility to themselves.
        if (wcscmp(pwszConsoleTitle, L"") == 0 && IsAvailableEastAsianCodePage(_pSettings->GetCodePage()) && g_uiOEMCP != _pSettings->GetCodePage())
        {
            _pSettings->SetCodePage(g_uiOEMCP);
        }
    }

    // Color table
    for (DWORD i = 0; i < COLOR_TABLE_SIZE; i++)
    {
        WCHAR awchBuffer[64];
        StringCchPrintfW(awchBuffer, ARRAYSIZE(awchBuffer), CONSOLE_REGISTRY_COLORTABLE, i);
        Status = RegistrySerialization::s_QueryValue(hTitleKey, awchBuffer, sizeof(dwValue), (PBYTE)& dwValue, nullptr);
        if (NT_SUCCESS(Status))
        {
            _pSettings->SetColorTableEntry(i, dwValue);
        }
    }

    GetEditKeys(hConsoleKey);

    // Close the registry keys
    CloseHandle(hTitleKey);
    CloseHandle(hConsoleKey);
    CloseHandle(hCurrentUserKey);
}