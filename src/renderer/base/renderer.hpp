/*++
Copyright (c) Microsoft Corporation

Module Name:
- Renderer.hpp

Abstract:
- This is the definition of our renderer.
- It provides interfaces for the console application to notify when various portions of the console state have changed and need to be redrawn.
- It requires a data interface to fetch relevant console structures required for drawing and a drawing engine target for output.

Author(s):
- Michael Niksa (MiNiksa) 17-Nov-2015
--*/

#pragma once

#include "..\inc\IRenderer.hpp"
#include "..\inc\IRenderEngine.hpp"
#include "..\inc\IRenderData.hpp"

#include "thread.hpp"
#include <deque>

#include "..\..\host\textBuffer.hpp"

namespace Microsoft
{
    namespace Console
    {
        namespace Render
        {
            class Renderer sealed : public IRenderer
            {
            public:
                static HRESULT s_CreateInstance(_In_ IRenderData* const pData,
                                                _In_reads_(cEngines) IRenderEngine** const rgpEngines,
                                                _In_ size_t const cEngines,
                                                _Outptr_result_nullonfailure_ Renderer** const ppRenderer);
                ~Renderer();

                HRESULT PaintFrame();

                void TriggerSystemRedraw(_In_ const RECT* const prcDirtyClient);
                void TriggerRedraw(_In_ const SMALL_RECT* const psrRegion);
                void TriggerRedraw(_In_ const COORD* const pcoord);
                void TriggerRedrawAll();

                void TriggerSelection();
                void TriggerScroll();
                void TriggerScroll(_In_ const COORD* const pcoordDelta);

                void TriggerFontChange(_In_ int const iDpi, _In_ FontInfoDesired const * const pFontInfoDesired, _Out_ FontInfo* const pFontInfo);

                HRESULT GetProposedFont(_In_ int const iDpi, _In_ FontInfoDesired const * const pFontInfoDesired, _Out_ FontInfo* const pFontInfo);

                COORD GetFontSize();
                bool IsCharFullWidthByFont(_In_ WCHAR const wch);

                void EnablePainting();
                void WaitForPaintCompletionAndDisable(const DWORD dwTimeoutMs);

                void AddRenderEngine(_In_ IRenderEngine* const pEngine);

            private:
                Renderer(_In_ IRenderData* const pData, _In_reads_(cEngines) IRenderEngine** const pEngine, _In_ size_t const cEngines);
                std::deque<IRenderEngine*> _rgpEngines;
                IRenderData* _pData;

                RenderThread* _pThread;
                void _NotifyPaintFrame();

                bool _CheckViewportAndScroll();

                HRESULT _PaintBackground(_In_ IRenderEngine* const pEngine);

                void _PaintBufferOutput(_In_ IRenderEngine* const pEngine);
                void _PaintBufferOutputRasterFontHelper(_In_ IRenderEngine* const pEngine, 
                                                        _In_ const ROW* const pRow, 
                                                        _In_reads_(cchLine) PCWCHAR const pwsLine, 
                                                        _In_reads_(cchLine) PBYTE pbKAttrsLine, 
                                                        _In_ size_t cchLine, 
                                                        _In_ size_t iFirstAttr, 
                                                        _In_ COORD const coordTarget);
                void _PaintBufferOutputColorHelper(_In_ IRenderEngine* const pEngine, 
                                                   _In_ const ROW* const pRow, 
                                                   _In_reads_(cchLine) PCWCHAR const pwsLine, 
                                                   _In_reads_(cchLine) PBYTE pbKAttrsLine, 
                                                   _In_ size_t cchLine, 
                                                   _In_ size_t iFirstAttr, 
                                                   _In_ COORD const coordTarget);
                HRESULT _PaintBufferOutputDoubleByteHelper(_In_ IRenderEngine* const pEngine,
                                                           _In_reads_(cchLine) PCWCHAR const pwsLine, 
                                                           _In_reads_(cchLine) PBYTE const pbKAttrsLine, 
                                                           _In_ size_t const cchLine, 
                                                           _In_ COORD const coordTarget);
                void _PaintBufferOutputGridLineHelper(_In_ IRenderEngine* const pEngine, _In_ const TextAttribute textAttribute, _In_ size_t const cchLine, _In_ COORD const coordTarget);

                void _PaintSelection(_In_ IRenderEngine* const pEngine);
                void _PaintCursor(_In_ IRenderEngine* const pEngine);

                void _PaintIme(_In_ IRenderEngine* const pEngine, _In_ const ConversionAreaInfo* const pAreaInfo, _In_ const TEXT_BUFFER_INFO* const pTextInfo);
                void _PaintImeCompositionString(_In_ IRenderEngine* const pEngine);

                HRESULT _UpdateDrawingBrushes(_In_ IRenderEngine* const pEngine, _In_ const TextAttribute attr, _In_ bool const fIncludeBackground);

                HRESULT _ClearOverlays(_In_ IRenderEngine* const pEngine);
                HRESULT _PerformScrolling(_In_ IRenderEngine* const pEngine);

                SMALL_RECT _srViewportPrevious;

                _Check_return_
                    NTSTATUS _GetSelectionRects(
                        _Outptr_result_buffer_all_(*pcRectangles) SMALL_RECT** const prgsrSelection,
                        _Out_ UINT* const pcRectangles) const;

                SMALL_RECT _RegionFromCoord(_In_ const COORD* const pcoord) const;
                COLORREF _ConvertAttrToRGB(_In_ const BYTE bAttr);
            };
        };
    };
};