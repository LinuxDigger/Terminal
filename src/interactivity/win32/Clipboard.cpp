/********************************************************
*                                                       *
*   Copyright (C) Microsoft. All rights reserved.       *
*                                                       *
********************************************************/

#include "precomp.h"

#include "clipboard.hpp"

#include "..\..\host\dbcs.h"
#include "..\..\host\scrolling.hpp"
#include "..\..\host\misc.h"
#include "..\..\host\output.h"

#include "..\inc\ServiceLocator.hpp"

#pragma hdrstop

// TODO: MSFT 14150722 - can these const values be generated at
// runtime without breaking compatibility?
static const WORD altScanCode = 0x38;
static const WORD leftShiftScanCode = 0x2A;

using namespace Microsoft::Console::Interactivity::Win32;

#pragma region Public Methods

void Clipboard::Copy()
{
    StoreSelectionToClipboard();   // store selection in clipboard
    Selection::Instance().ClearSelection();   // clear selection in console
}

/*++

Perform paste request into old app by pulling out clipboard
contents and writing them to the console's input buffer

--*/
void Clipboard::Paste()
{
    HANDLE ClipboardDataHandle;

    // Clear any selection or scrolling that may be active.
    Selection::Instance().ClearSelection();
    Scrolling::s_ClearScroll();

    // Get paste data from clipboard
    if (!OpenClipboard(ServiceLocator::LocateConsoleWindow()->GetWindowHandle()))
    {
        return;
    }

    ClipboardDataHandle = GetClipboardData(CF_UNICODETEXT);
    if (ClipboardDataHandle == nullptr)
    {
        CloseClipboard();
        return;
    }

    PWCHAR pwstr = (PWCHAR)GlobalLock(ClipboardDataHandle);
    StringPaste(pwstr, (ULONG)GlobalSize(ClipboardDataHandle) / sizeof(WCHAR));
    GlobalUnlock(ClipboardDataHandle);

    CloseClipboard();
}

Clipboard& Clipboard::Instance()
{
    static Clipboard clipboard;
    return clipboard;
}

// Routine Description:
// - This routine pastes given Unicode string into the console window.
// Arguments:
// - pData - Unicode string that is pasted to the console window
// - cchData - Size of the Unicode String in characters
// Return Value:
// - None
void Clipboard::StringPaste(_In_reads_(cchData) const wchar_t* const pData,
                            _In_ const size_t cchData)
{
    if (pData == nullptr)
    {
        return;
    }

    CONSOLE_INFORMATION* const gci = ServiceLocator::LocateGlobals()->getConsoleInformation();

    try
    {
        std::deque<std::unique_ptr<IInputEvent>> inEvents = TextToKeyEvents(pData, cchData);
        gci->pInputBuffer->Write(inEvents);
    }
    catch (...)
    {
        LOG_HR(wil::ResultFromCaughtException());
    }
}

#pragma endregion

#pragma region Private Methods

// Routine Description:
// - converts a wchar_t* into a series of KeyEvents as if it was typed
// from the keyboard
// Arguments:
// - pData - the text to convert
// - cchData - the size of pData, in wchars
// Return Value:
// - deque of KeyEvents that represent the string passed in
// Note:
// - will throw exception on error
std::deque<std::unique_ptr<IInputEvent>> Clipboard::TextToKeyEvents(_In_reads_(cchData) const wchar_t* const pData,
                                                                    _In_ const size_t cchData)
{
    THROW_IF_NULL_ALLOC(pData);

    std::deque<std::unique_ptr<IInputEvent>> keyEvents;

    for (size_t i = 0; i < cchData; ++i)
    {
        wchar_t currentChar = pData[i];

        // filter out linefeed if it's not the first char and preceded
        // by a carriage return
        if (!(FilterCharacterOnPaste(&currentChar) &&
              (currentChar != UNICODE_LINEFEED ||
               i == 0 ||
               pData[i - 1] != UNICODE_CARRIAGERETURN)))
        {
            continue;
        }

        if (currentChar == 0)
        {
            break;
        }

        const short invalidKey = -1;
        short keyState = VkKeyScanW(currentChar);

        if (keyState == invalidKey)
        {
            WORD CharType;

            // Determine DBCS character because these character does not know by VkKeyScan.
            // GetStringTypeW(CT_CTYPE3) & C3_ALPHA can determine all linguistic characters. However, this is
            // not include symbolic character for DBCS.
            //
            // IsCharFullWidth can help for DBCS symbolic character.
            GetStringTypeW(CT_CTYPE3, &currentChar, 1, &CharType);

            if (IsFlagSet(CharType, C3_ALPHA) || IsCharFullWidth(currentChar))
            {
                keyState = 0;
            }
        }

        std::deque<std::unique_ptr<KeyEvent>> convertedEvents;
        if (keyState == invalidKey)
        {
            // if VkKeyScanW fails (char is not in kbd layout), we must
            // emulate the key being input through the numpad
            convertedEvents = CharToNumpad(currentChar);
        }
        else
        {
            convertedEvents = CharToKeyboardEvents(currentChar, keyState);
        }

        while (!convertedEvents.empty())
        {
            keyEvents.push_back(std::move(convertedEvents.front()));
            convertedEvents.pop_front();
        }
    }
    return keyEvents;
}

// Routine Description:
// - converts a wchar_t into a series of KeyEvents as if it was typed
// using the keyboard
// Arguments:
// - wch - the wchar_t to convert
// Return Value:
// - deque of KeyEvents that represent the wchar_t being typed
// Note:
// - will throw exception on error
std::deque<std::unique_ptr<KeyEvent>> Clipboard::CharToKeyboardEvents(_In_ const wchar_t wch, _In_ const short keyState)
{
    const byte modifierState = HIBYTE(keyState);

    bool altGrSet = false;
    bool shiftSet = false;
    std::deque<std::unique_ptr<KeyEvent>> keyEvents;

    // add modifier key event if necessary
    if (AreAllFlagsSet(modifierState, VkKeyScanModState::CtrlAndAltPressed))
    {
        altGrSet = true;
        keyEvents.push_back(std::make_unique<KeyEvent>(TRUE,
                                                       1ui16,
                                                       static_cast<WORD>(VK_MENU),
                                                       altScanCode,
                                                       UNICODE_NULL,
                                                       (ENHANCED_KEY | LEFT_CTRL_PRESSED | RIGHT_ALT_PRESSED)));
    }
    else if (IsFlagSet(modifierState, VkKeyScanModState::ShiftPressed))
    {
        shiftSet = true;
        keyEvents.push_back(std::make_unique<KeyEvent>(TRUE,
                                                       1ui16,
                                                       static_cast<WORD>(VK_SHIFT),
                                                       leftShiftScanCode,
                                                       UNICODE_NULL,
                                                       SHIFT_PRESSED));
    }

    const WORD virtualScanCode = static_cast<WORD>(MapVirtualKeyW(wch, MAPVK_VK_TO_VSC));
    KeyEvent keyEvent{ TRUE, 1, LOBYTE(keyState), virtualScanCode, wch, 0 };

    // add modifier flags if necessary
    SetFlagIf(keyEvent._activeModifierKeys, SHIFT_PRESSED, IsFlagSet(modifierState, VkKeyScanModState::ShiftPressed));
    SetFlagIf(keyEvent._activeModifierKeys, LEFT_CTRL_PRESSED, IsFlagSet(modifierState, VkKeyScanModState::CtrlPressed));
    SetFlagIf(keyEvent._activeModifierKeys, RIGHT_ALT_PRESSED, AreAllFlagsSet(modifierState, VkKeyScanModState::CtrlAndAltPressed));

    // add key event down and up
    keyEvents.push_back(std::make_unique<KeyEvent>(keyEvent));
    keyEvent._keyDown = FALSE;
    keyEvents.push_back(std::make_unique<KeyEvent>(keyEvent));

    // add modifier key up event
    if (altGrSet)
    {
        keyEvents.push_back(std::make_unique<KeyEvent>(FALSE,
                                                       1ui16,
                                                       static_cast<WORD>(VK_MENU),
                                                       altScanCode,
                                                       UNICODE_NULL,
                                                       ENHANCED_KEY));
    }
    else if (shiftSet)
    {
        keyEvents.push_back(std::make_unique<KeyEvent>(FALSE,
                                                       1ui16,
                                                       static_cast<WORD>(VK_SHIFT),
                                                       leftShiftScanCode,
                                                       UNICODE_NULL,
                                                       0));
    }

    return keyEvents;
}

// Routine Description:
// - converts a wchar_t into a series of KeyEvents as if it was typed
// using Alt + numpad
// Arguments:
// - wch - the wchar_t to convert
// Return Value:
// - deque of KeyEvents that represent the wchar_t being typed using
// alt + numpad
// Note:
// - will throw exception on error
std::deque<std::unique_ptr<KeyEvent>> Clipboard::CharToNumpad(_In_ const wchar_t wch)
{
    const UINT codepage = ServiceLocator::LocateGlobals()->getConsoleInformation()->OutputCP;
    const int radix = 10;

    std::deque<std::unique_ptr<KeyEvent>> keyEvents;

    std::wstring wstr{ wch };
    std::deque<char> convertedChars;
    convertedChars = ConvertToOem(codepage, wstr);

    for (char ch : convertedChars)
    {
        // char values are in the range [-128, 127] so we need to be
        // able to store up to 4 chars from the conversion
        char charString[4] = { 0 };
        THROW_HR_IF(E_INVALIDARG, 0 != _itoa_s(ch, charString, ARRAYSIZE(charString), radix));

        keyEvents.push_back(std::make_unique<KeyEvent>(TRUE,
                                                       1ui16,
                                                       static_cast<WORD>(VK_MENU),
                                                       altScanCode,
                                                       UNICODE_NULL,
                                                       LEFT_ALT_PRESSED));
        for (size_t i = 0; i < ARRAYSIZE(charString); ++i)
        {
            if (charString[i] == 0)
            {
                break;
            }
            const WORD virtualKey = charString[i] + '0' + VK_NUMPAD0;
            const WORD virtualScanCode = static_cast<WORD>(MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC));

            keyEvents.push_back(std::make_unique<KeyEvent>(TRUE,
                                                           1ui16,
                                                           virtualKey,
                                                           virtualScanCode,
                                                           UNICODE_NULL,
                                                           LEFT_ALT_PRESSED));
            keyEvents.push_back(std::make_unique<KeyEvent>(FALSE,
                                                           1ui16,
                                                           virtualKey,
                                                           virtualScanCode,
                                                           UNICODE_NULL,
                                                           LEFT_ALT_PRESSED));
        }
        keyEvents.push_back(std::make_unique<KeyEvent>(FALSE,
                                                       1ui16,
                                                       static_cast<WORD>(VK_MENU),
                                                       altScanCode,
                                                       wch,
                                                       0));
    }
    return keyEvents;
}

// Routine Description:
// - Copies the selected area onto the global system clipboard.
// Arguments:
//  <none>
// Return Value:
//  <none>
void Clipboard::StoreSelectionToClipboard()
{
    const CONSOLE_INFORMATION* const gci = ServiceLocator::LocateGlobals()->getConsoleInformation();
    Selection* pSelection = &Selection::Instance();

    // See if there is a selection to get
    if (!pSelection->IsAreaSelected())
    {
        return;
    }

    // read selection area.
    SCREEN_INFORMATION* const pScreenInfo = gci->CurrentScreenBuffer;

    SMALL_RECT* rgsrSelection;
    UINT cRectsSelected;

    NTSTATUS status = pSelection->GetSelectionRects(&rgsrSelection, &cRectsSelected);

    if (NT_SUCCESS(status))
    {
        PWCHAR* rgTempRows = new PWCHAR[cRectsSelected];
        status = NT_TESTNULL(rgTempRows);

        if (NT_SUCCESS(status))
        {
            size_t* rgTempRowLengths = new size_t[cRectsSelected];
            status = NT_TESTNULL(rgTempRowLengths);

            if (NT_SUCCESS(status))
            {
                const bool fLineSelection = Selection::Instance().IsLineSelection();

                status = RetrieveTextFromBuffer(pScreenInfo,
                    fLineSelection,
                    cRectsSelected,
                    rgsrSelection,
                    rgTempRows,
                    rgTempRowLengths);

                if (NT_SUCCESS(status))
                {
                    status = CopyTextToSystemClipboard(cRectsSelected, rgTempRows, rgTempRowLengths);

                    for (UINT iRow = 0; iRow < cRectsSelected; iRow++)
                    {
                        if (rgTempRows[iRow] != nullptr)
                        {
                            delete rgTempRows[iRow];
                        }
                    }
                }

                delete[] rgTempRowLengths;
            }

            delete[] rgTempRows;
        }

        delete[] rgsrSelection;
    }
}

_Check_return_
NTSTATUS Clipboard::RetrieveTextFromBuffer(_In_ SCREEN_INFORMATION* const pScreenInfo,
                                           _In_ bool const fLineSelection,
                                           _In_ UINT const cRectsSelected,
                                           _In_reads_(cRectsSelected) const SMALL_RECT* const rgsrSelection,
                                           _Out_writes_(cRectsSelected) PWCHAR* const rgpwszTempText,
                                           _Out_writes_(cRectsSelected) size_t* const rgTempTextLengths)
{
    NTSTATUS status = STATUS_SUCCESS;

    if (rgpwszTempText == nullptr)
    {
        status = STATUS_INVALID_PARAMETER_5;
    }
    else if (rgTempTextLengths == nullptr)
    {
        status = STATUS_INVALID_PARAMETER_6;
    }

    if (NT_SUCCESS(status))
    {
        UINT iRect = 0;

        // for each row in the selection
        for (iRect = 0; iRect < cRectsSelected; iRect++)
        {
            SMALL_RECT srHighlightRow = rgsrSelection[iRect];

            SMALL_RECT srSelection;
            Selection::Instance().GetSelectionRectangle(&srSelection);

            const UINT iRow = srSelection.Top + iRect;

            // recalculate string length again as the width of the highlight row might have been reduced in the bisect call
            short sStringLength = srHighlightRow.Right - srHighlightRow.Left + 1;

            // this is the source location X/Y coordinates within the active screen buffer to start copying from
            COORD coordSourcePoint;
            coordSourcePoint.X = srHighlightRow.Left;
            coordSourcePoint.Y = srHighlightRow.Top;

            // this is how much to select from the source location. we want one row at a time and the width highlighted.
            COORD coordSelectionSize;
            coordSelectionSize.X = sStringLength;
            coordSelectionSize.Y = 1;

            // allocate our output buffer to copy into for further manipulation
            CHAR_INFO* rgSelection = new CHAR_INFO[sStringLength];
            status = NT_TESTNULL(rgSelection);

            if (NT_SUCCESS(status))
            {
                // our output buffer is 1 dimensional and is just as long as the string, so the "rectangle" should specify just a line.
                SMALL_RECT srTargetRect;
                srTargetRect.Left = 0;
                srTargetRect.Right = sStringLength - 1; // length of 80 runs from left 0 to right 79. therefore -1.
                srTargetRect.Top = 0;
                srTargetRect.Bottom = 0;

                // retrieve the data from the screen buffer
                #pragma prefast(suppress: 6001, "rgSelection's initial state doesn't matter and will be filled appropriately by the called function.")
                status = ReadRectFromScreenBuffer(pScreenInfo, coordSourcePoint, rgSelection, coordSelectionSize, &srTargetRect, nullptr);
                if (NT_SUCCESS(status))
                {
                    // allocate a string buffer
                    size_t cSelectionLength = sStringLength + 1; // add one for null trailing character
                    PWCHAR pwszSelection = new WCHAR[cSelectionLength + 2]; // add 2 to leave space for \r\n if we munged it.
                    status = NT_TESTNULL(rgSelection);

                    if (NT_SUCCESS(status))
                    {
                        // position in the selection length string as we right.
                        // this may not align with the source if we skip characters (trailing bytes)
                        UINT cSelPos = 0;

                        // copy character into the string buffer
                        for (short iCol = 0; iCol < sStringLength; iCol++)
                        {
                            // strip any characters marked "trailing byte" as they're a duplicate
                            // e.g. only copy characters that are NOT trailing bytes
                            if ((rgSelection[iCol].Attributes & COMMON_LVB_TRAILING_BYTE) == 0)
                            {
                                pwszSelection[cSelPos] = rgSelection[iCol].Char.UnicodeChar;
                                cSelPos++;
                            }
                        }

                        // replace any null characters with spaces
                        //for (int iCol = 0; iCol < sStringLength; iCol++)
                        //{
                        //    if (pwszSelection[iCol] == UNICODE_NULL)
                        //    {
                        //        pwszSelection[iCol] = UNICODE_SPACE;
                        //    }
                        //}

                        sStringLength = (short)cSelPos;

                        // trim trailing spaces
                        BOOL bMungeData = (GetKeyState(VK_SHIFT) & KEY_PRESSED) == 0;
                        if (bMungeData)
                        {
                            ROW* pRow = pScreenInfo->TextInfo->GetRowByOffset(iRow);

                            if (pRow == nullptr)
                            {
                                status = STATUS_UNSUCCESSFUL;
                                break;
                            }

                            // FOR LINE SELECTION ONLY: if the row was wrapped, don't remove the spaces at the end.
                            if (!fLineSelection
                                || !pRow->CharRow.WasWrapForced())
                            {
                                for (int iCol = (int)(sStringLength - 1); iCol >= 0; iCol--)
                                {
                                    if (pwszSelection[iCol] == UNICODE_SPACE)
                                    {
                                        pwszSelection[iCol] = UNICODE_NULL;
                                    }
                                    else
                                    {
                                        break;
                                    }

                                }
                            }
                        }

                        // ensure the last character is a null
                        pwszSelection[cSelectionLength - 1] = UNICODE_NULL;

                        // remeasure string
                        if (FAILED(StringCchLengthW(pwszSelection, cSelectionLength, &cSelectionLength)))
                        {
                            status = STATUS_UNSUCCESSFUL;
                        }

                        if (NT_SUCCESS(status))
                        {
                            // if we munged (trimmed spaces), apply CR/LF to the end of the final string
                            if (bMungeData)
                            {
                                // if we're the final line, do not apply CR/LF.
                                // a.k.a if we're earlier than the bottom, then apply CR/LF.
                                if (iRect < cRectsSelected - 1)
                                {
                                    // FOR LINE SELECTION ONLY: if the row was wrapped, do not apply CR/LF.
                                    // a.k.a. if the row was NOT wrapped, then we can assume a CR/LF is proper
                                    // always apply \r\n for box selection
                                    if (!fLineSelection
                                        || !pScreenInfo->TextInfo->GetRowByOffset(iRow)->CharRow.WasWrapForced())
                                    {
                                        pwszSelection[cSelectionLength++] = UNICODE_CARRIAGERETURN;
                                        pwszSelection[cSelectionLength++] = UNICODE_LINEFEED;
                                    }

                                    // and ensure the string is null terminated.
                                    pwszSelection[cSelectionLength] = UNICODE_NULL;
                                }
                            }

                            // save the formatted string and its length for later
                            rgpwszTempText[iRect] = pwszSelection;
                            rgTempTextLengths[iRect] = cSelectionLength;
                        }
                    }

                }

                delete[] rgSelection;
            }

            if (!NT_SUCCESS(status))
            {
                break;
            }
        }

        if (!NT_SUCCESS(status))
        {
            for (UINT iDelRect = 0; iDelRect < iRect; iDelRect++)
            {
                if (rgpwszTempText[iDelRect] != nullptr)
                {
                    delete[] rgpwszTempText[iDelRect];
                }
            }
        }
    }

    return status;
}


// Routine Description:
// - Copies the text given onto the global system clipboard.
// Arguments:
// - cTotalRows
// Return Value:
//  <none>
NTSTATUS Clipboard::CopyTextToSystemClipboard(_In_ const UINT cTotalRows,
                                              _In_reads_(cTotalRows) const PWCHAR* const rgTempRows,
                                              _In_reads_(cTotalRows) const size_t* const rgTempRowLengths)
{
    NTSTATUS status = STATUS_SUCCESS;

    if (rgTempRows == nullptr)
    {
        status = STATUS_INVALID_PARAMETER_2;
    }
    else if (rgTempRowLengths == nullptr)
    {
        status = STATUS_INVALID_PARAMETER_3;
    }

    if (NT_SUCCESS(status))
    {

        // calculate number of characters in the rows we created
        size_t cRowCharCount = 0;

        for (UINT iRow = 0; iRow < cTotalRows; iRow++)
        {
            size_t cLength;

            if (FAILED(StringCchLengthW(rgTempRows[iRow], rgTempRowLengths[iRow] + 1, &cLength)))
            {
                status = STATUS_UNSUCCESSFUL;
                break;
            }

            cRowCharCount += cLength;
        }

        if (NT_SUCCESS(status))
        {
            // +1 for null terminator at end of clipboard data
            cRowCharCount++;

            ASSERT(cRowCharCount > 0);

            // allocate the final clipboard data
            HANDLE hClipboardDataHandle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, cRowCharCount * sizeof(WCHAR));

            if (hClipboardDataHandle == nullptr)
            {
                status = NTSTATUS_FROM_WIN32(GetLastError());

                // if for some reason Get Last Error returned successful, set generic memory error.
                if (NT_SUCCESS(status))
                {
                    status = STATUS_NO_MEMORY;
                }
            }

            if (NT_SUCCESS(status))
            {
                PWCHAR pwszClipboardPos = (PWCHAR)GlobalLock(hClipboardDataHandle);

                if (pwszClipboardPos == nullptr)
                {
                    status = NTSTATUS_FROM_WIN32(GetLastError());

                    // if Get Last Error was successful for some reason, set generic memory error
                    if (NT_SUCCESS(status))
                    {
                        status = STATUS_NO_MEMORY;
                    }
                }

                if (NT_SUCCESS(status))
                {
                    // copy all text into the final clipboard data handle. There should be no nulls between rows of
                    // characters, but there should be a \0 at the end.
                    for (UINT iRow = 0; iRow < cTotalRows; iRow++)
                    {
                        if (FAILED(StringCchCopyW(pwszClipboardPos, rgTempRowLengths[iRow] + 1, rgTempRows[iRow])))
                        {
                            status = STATUS_UNSUCCESSFUL;
                            break;
                        }

                        pwszClipboardPos += rgTempRowLengths[iRow];
                    }

                    *pwszClipboardPos = UNICODE_NULL;

                    GlobalUnlock(hClipboardDataHandle);

                    if (NT_SUCCESS(status))
                    {
                        BOOL fSuccess = OpenClipboard(ServiceLocator::LocateConsoleWindow()->GetWindowHandle());
                        if (!fSuccess)
                        {
                            status = NTSTATUS_FROM_WIN32(GetLastError());
                        }

                        if (NT_SUCCESS(status))
                        {
                            fSuccess = EmptyClipboard();
                            if (!fSuccess)
                            {
                                status = NTSTATUS_FROM_WIN32(GetLastError());
                            }

                            if (NT_SUCCESS(status))
                            {
                                if (SetClipboardData(CF_UNICODETEXT, hClipboardDataHandle) == nullptr)
                                {
                                    status = NTSTATUS_FROM_WIN32(GetLastError());
                                }
                            }

                            if (!CloseClipboard() && NT_SUCCESS(status))
                            {
                                status = NTSTATUS_FROM_WIN32(GetLastError());
                            }
                        }
                    }
                }

                if (!NT_SUCCESS(status))
                {
                    // only free if we failed.
                    // the memory has to remain allocated if we successfully placed it on the clipboard.
                    GlobalFree(hClipboardDataHandle);
                }
            }
        }
    }

    return status;
}

// Returns true if the character should be emitted to the paste stream
// -- in some cases, we will change what character should be emitted, as in the case of "smart quotes"
// Returns false if the character should not be emitted (e.g. <TAB>)
bool Clipboard::FilterCharacterOnPaste(_Inout_ WCHAR * const pwch)
{
    const CONSOLE_INFORMATION* const gci = ServiceLocator::LocateGlobals()->getConsoleInformation();
    bool fAllowChar = true;
    if (gci->GetFilterOnPaste() &&
        (IsFlagSet(gci->pInputBuffer->InputMode, ENABLE_PROCESSED_INPUT)))
    {
        switch (*pwch)
        {
            // swallow tabs to prevent inadvertant tab expansion
            case UNICODE_TAB:
            {
                fAllowChar = false;
                break;
            }

            // Replace Unicode space with standard space
            case UNICODE_NBSP:
            case UNICODE_NARROW_NBSP:
            {
                *pwch = UNICODE_SPACE;
                break;
            }

            // Replace "smart quotes" with "dumb ones"
            case UNICODE_LEFT_SMARTQUOTE:
            case UNICODE_RIGHT_SMARTQUOTE:
            {
                *pwch = UNICODE_QUOTE;
                break;
            }

            // Replace Unicode dashes with a standard hypen
            case UNICODE_EM_DASH:
            case UNICODE_EN_DASH:
            {
                *pwch = UNICODE_HYPHEN;
                break;
            }
        }
    }

    return fAllowChar;
}

#pragma endregion