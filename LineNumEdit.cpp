// LineNumEdit.cpp --- textbox with line numbers

#include "LineNumEdit.hpp"

/////////////////////////////////////////////////////////////////////////////////////////
// LineNumStatic

LineNumStatic::LineNumStatic(HWND hwnd)
    : LineNumBase(hwnd)
    , m_rgbText(::GetSysColor(COLOR_WINDOWTEXT))
    , m_rgbBack(::GetSysColor(COLOR_3DFACE))
    , m_linedelta(1)
    , m_hbm(NULL)
    , m_siz { 0, 0 }
    , m_bstrTextCache(NULL)
    , m_cchCache(0)
    , m_cLogicalLinesCache(0)
    , m_bTextCacheValid(FALSE)
{
    ::SHStrDup(TEXT("%d"), &m_format);
}

LineNumStatic::~LineNumStatic()
{
    ::DeleteObject(m_hbm);
    ::CoTaskMemFree(m_format);
    ::SysFreeString(m_bstrTextCache);
}

LRESULT CALLBACK
LineNumStatic::WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT rc;
    POINT pt;
    PAINTSTRUCT ps;
    HWND hwndEdit;
    switch (uMsg)
    {
    case WM_PAINT:
        if (HDC hDC = ::BeginPaint(hwnd, &ps))
        {
            OnDrawClient(hwnd, hDC);
            ::EndPaint(hwnd, &ps);
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_MOUSEMOVE:
        hwndEdit = GetEdit();
        GetClientRect(hwnd, &rc);
        pt.x = rc.right + 1;
        pt.y = GET_Y_LPARAM(lParam);
        ::MapWindowPoints(hwnd, hwndEdit, &pt, 1);
        return SendMessage(hwndEdit, uMsg, wParam, MAKELPARAM(pt.x, pt.y));
    case WM_ERASEBKGND:
        return TRUE;
    case WM_DESTROY:
        DeleteProps(hwnd);
        break;
    case WM_MOUSEWHEEL:
        hwndEdit = GetEdit();
        PostMessage(hwndEdit, uMsg, wParam, lParam);
        break;
    }
    return DefWndProc(hwnd, uMsg, wParam, lParam);
}

void LineNumStatic::OnDrawClient(HWND hwnd, HDC hDC)
{
    // get the client size
    RECT rcClient;
    ::GetClientRect(hwnd, &rcClient);
    SIZE siz = { rcClient.right - rcClient.left, rcClient.bottom - rcClient.top };
    if (!siz.cx || !siz.cy)
        return;

    // prepare for double buffering
    HDC hdcMem = ::CreateCompatibleDC(hDC);
    HBITMAP hbm;
    if (m_hbm && siz.cx <= m_siz.cx && siz.cy <= m_siz.cy)
    {
        hbm = m_hbm;
    }
    else
    {
        hbm = ::CreateCompatibleBitmap(hDC, siz.cx, siz.cy);
        m_siz = siz;
    }
    HGDIOBJ hbmOld = ::SelectObject(hdcMem, hbm);

    // fill background
    UINT uMsg;
    HWND hwndEdit = GetEdit();
    if (!::IsWindowEnabled(hwndEdit) || (GetWindowLong(hwndEdit, GWL_STYLE) & ES_READONLY))
        uMsg = WM_CTLCOLORSTATIC;
    else
        uMsg = WM_CTLCOLOREDIT;
    HBRUSH hbr = reinterpret_cast<HBRUSH>(
        ::SendMessage(GetParent(hwndEdit), uMsg,
                      reinterpret_cast<WPARAM>(hDC), reinterpret_cast<LPARAM>(hwndEdit)));
    ::FillRect(hdcMem, &rcClient, hbr);

    // get top margin and line height
    RECT rcEdit;
    Edit_GetRect(hwndEdit, &rcEdit);
    INT yLine = rcEdit.top, cyLine = GetLineHeight();

    // get margins
    DWORD dwMargins = DWORD(::SendMessage(hwndEdit, EM_GETMARGINS, 0, 0));
    INT leftmargin = LOWORD(dwMargins), rightmargin = HIWORD(dwMargins);

    // Remember the full (unshrunk) client width so the final BitBlt below
    // can still copy the whole client area to the screen DC. rcClient.right
    // is about to be shrunk by leftmargin for the purposes of drawing the
    // separator line and text columns; if the BitBlt used the shrunk width
    // instead, the rightmost leftmargin-wide strip (right next to the edit
    // control) would never be copied to the screen, leaving stale pixels
    // from a previous paint visible there -- most noticeable when the
    // column width changes, e.g. on Ctrl+Wheel zoom.
    INT cxFull = rcClient.right;

    // shrink rectangle
    rcClient.right -= leftmargin;
    siz.cx -= leftmargin;

    // fill background
    hbr = ::CreateSolidBrush(m_rgbBack);
    ::FillRect(hdcMem, &rcClient, hbr);
    ::DeleteObject(hbr);

    // right line
    HPEN hPen = ::CreatePen(PS_SOLID, 0, m_rgbText);
    HGDIOBJ hPenOld = ::SelectObject(hdcMem, hPen);
    ::MoveToEx(hdcMem, rcClient.right - 1, rcClient.top, NULL);
    ::LineTo(hdcMem, rcClient.right - 1, rcClient.bottom);
    ::DeleteObject(::SelectObject(hdcMem, hPenOld));

    // draw lines
    WCHAR szText[32];
    HFONT hFont = GetWindowFont(hwndEdit);
    HGDIOBJ hFontOld = ::SelectObject(hdcMem, hFont);
    ::SetBkMode(hdcMem, TRANSPARENT);
    {
        // Rebuild the cached copy of the edit text (and its total logical
        // line count) only if it was invalidated by an actual text change.
        // A pure scroll/caret repaint -- e.g. mouse-wheel scrolling, which
        // can fire many repaints back-to-back -- reuses the cache as-is,
        // instead of re-copying and re-scanning the whole document on every
        // single repaint.
        if (!m_bTextCacheValid)
        {
            ::SysFreeString(m_bstrTextCache);
            m_bstrTextCache = NULL;

            INT cch = Edit_GetTextLength(hwndEdit);
            m_bstrTextCache = ::SysAllocStringLen(NULL, cch);
            if (m_bstrTextCache)
            {
                Edit_GetText(hwndEdit, m_bstrTextCache, cch + 1);
                m_cchCache = cch;

                m_cLogicalLinesCache = 0;
                for (LPCTSTR psz = m_bstrTextCache; *psz; ++psz)
                {
                    if (*psz == TEXT('\n'))
                        ++m_cLogicalLinesCache;
                }
            }
            m_bTextCacheValid = TRUE;
        }

        BSTR bstrText = m_bstrTextCache;
        if (bstrText)
        {
            INT cch = m_cchCache;
            INT cLogicalLines = m_cLogicalLinesCache;

            // initialize variables for lines loop
            INT iPhysicalLine = Edit_GetFirstVisibleLine(hwndEdit);
            INT ich = Edit_LineIndex(hwndEdit, iPhysicalLine);
            if (ich == -1) // beyond the limit
                ich = cch;
            INT ichOld = ich;

            // Newlines up to the first visible char still need to be
            // counted per paint since the viewport (and so ich) can change,
            // but this is now a scan of just [0, ich) rather than also
            // re-copying and re-scanning the entire document as before.
            INT iLogicalLine = 0;
            for (INT i = 0; i < ich && bstrText[i]; ++i)
            {
                if (bstrText[i] == L'\n')
                    ++iLogicalLine;
            }

            INT iOldLogicalLine;
            if (ich == 0)
                iOldLogicalLine = -1;
            else if (ich > 0 && bstrText[ich - 1] == L'\n')
                iOldLogicalLine = iLogicalLine - 1;
            else
                iOldLogicalLine = iLogicalLine;

            do // for each physical lines
            {
                RECT rc = { 0, yLine, siz.cx - 1, yLine + cyLine }; // one line
                INT nLabel = iLogicalLine + m_linedelta; // label

                // fill the background if necessary, and set text color
                HANDLE hProp = ::GetProp(hwnd, GetPropName(nLabel));
                if (hProp &&
                    (ich < cch || iOldLogicalLine < iLogicalLine || iLogicalLine < cLogicalLines))
                {
                    COLORREF rgbBack = (COLORREF(reinterpret_cast<ULONG_PTR>(hProp)) & 0xFFFFFF);
                    HBRUSH hbr = ::CreateSolidBrush(rgbBack);
                    ::FillRect(hdcMem, &rc, hbr);
                    ::DeleteObject(hbr);
                    INT value = (GetRValue(rgbBack) + GetGValue(rgbBack) + GetBValue(rgbBack)) / 3;
                    if (value < 255 / 3)
                        ::SetTextColor(hdcMem, RGB(255, 255, 255));
                    else
                        ::SetTextColor(hdcMem, RGB(0, 0, 0));
                }
                else
                {
                    ::SetTextColor(hdcMem, m_rgbText);
                }

                // draw line text
                if (ich <= cch && iOldLogicalLine != iLogicalLine)
                {
                    StringCchPrintfW(szText, _countof(szText), m_format, nLabel);
                    rc.right -= rightmargin;
                    UINT uFormat = DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
                    ::DrawTextW(hdcMem, szText, ::lstrlenW(szText), &rc, uFormat);
                }

                // go to next line
                yLine += cyLine;
                ++iPhysicalLine;

                // Jump straight to the start of the next physical line with a
                // single EM_LINEINDEX call. This replaces the old approach of
                // scanning character-by-character and calling
                // Edit_LineFromChar (a window message) once per character,
                // which was O(line length) messages per drawn line.
                ichOld = ich;
                INT ichNext = Edit_LineIndex(hwndEdit, iPhysicalLine);
                ich = (ichNext == -1) ? cch : ichNext;

                // Update the logical line incrementally by counting newlines
                // only in the [ichOld, ich) span just advanced over, instead
                // of rescanning the buffer from the beginning every
                // iteration (which made this O(n) per drawn line).
                iOldLogicalLine = iLogicalLine;
                for (INT i = ichOld; i < ich; ++i)
                {
                    if (bstrText[i] == L'\n')
                        ++iLogicalLine;
                }
                if (iLogicalLine == iOldLogicalLine && ich == ichOld)
                    break;
            } while (yLine < rcClient.bottom); // beyond the client area?

            // NOTE: bstrText is the persistent cache (m_bstrTextCache), not
            // a local allocation, so it is intentionally NOT freed here. It
            // is freed/rebuilt in the "!m_bTextCacheValid" block above, and
            // in the destructor.
        }
    }
    ::SelectObject(hdcMem, hFontOld);

    // send the image to the window (use the full width, not the
    // margin-shrunk rcClient.right, or the rightmost strip never gets
    // copied to the screen and keeps showing stale pixels)
    ::BitBlt(hDC, 0, 0, cxFull, rcClient.bottom, hdcMem, 0, 0, SRCCOPY);
    ::SelectObject(hdcMem, hbmOld);

    // clean up
    ::DeleteDC(hdcMem);

    // do cache
    if (m_hbm != hbm)
    {
        DeleteObject(m_hbm);
        m_hbm = hbm;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
// LineNumEdit

void LineNumEdit::Prepare()
{
    if (m_bInPrepare)
        return;
    m_bInPrepare = TRUE;

    // sanity check
    assert(::IsWindow(m_hwnd));
    assert(!!(::GetWindowLong(m_hwnd, GWL_STYLE) & WS_CHILD));
    assert(!!(::GetWindowLong(m_hwnd, GWL_STYLE) & ES_MULTILINE));

    RECT rcClient;
    ::GetClientRect(m_hwnd, &rcClient);
    INT cxColumn = GetColumnWidth(), cyColumn = rcClient.bottom - rcClient.top;

    // get margins
    DWORD dwMargins = DWORD(::SendMessage(m_hwnd, EM_GETMARGINS, 0, 0));
    INT leftmargin = LOWORD(dwMargins), rightmargin = HIWORD(dwMargins);

    // adjust rectangle
    RECT rcEdit = rcClient;
    rcEdit.left += cxColumn;
    rcEdit.right -= rightmargin;
    Edit_SetRectNoPaint(m_hwnd, &rcEdit);

    if (m_hwndStatic)
    {
        ::MoveWindow(m_hwndStatic, 0, 0, cxColumn, cyColumn, FALSE);
    }
    else
    {
        DWORD style = WS_CHILD | WS_VISIBLE | SS_NOTIFY;
        HWND hwndStatic = ::CreateWindow(TEXT("STATIC"), NULL, style,
                                         0, 0, cxColumn, cyColumn, m_hwnd,
                                         NULL, ::GetModuleHandle(NULL), NULL);
        m_hwndStatic.Attach(hwndStatic);
    }

    m_bInPrepare = FALSE;
}

LRESULT CALLBACK
LineNumEdit::WindowProcDx(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LRESULT ret;
    switch (uMsg)
    {
    case WM_ENABLE: case WM_SYSCOLORCHANGE:
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        RefreshColors();
        return ret;
    case WM_PAINT:
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        if (m_bSetRedraw)
            m_hwndStatic.Redraw();
        return ret;
    case EM_SETREADONLY:
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        RefreshColors(FALSE);
        return ret;
    case LNEM_SETLINENUMFORMAT:
        SetLineNumberFormat(reinterpret_cast<LPCTSTR>(lParam));
        return 0;
    case LNEM_SETNUMOFDIGITS:
        SetNumberOfDigits(INT(wParam));
        return 0;
    case LNEM_SETLINEMARK:
        {
            LPCTSTR pszName = m_hwndStatic.GetPropName(INT(wParam));
            ::RemoveProp(m_hwndStatic, pszName);
            COLORREF rgb = COLORREF(lParam);
            if (rgb != CLR_INVALID)
            {
                lParam |= 0xFF000000;
                ::SetProp(m_hwndStatic, pszName, reinterpret_cast<HANDLE>(lParam));
            }
        }
        if (m_bSetRedraw)
            m_hwndStatic.Redraw();
        return 0;
    case LNEM_CLEARLINEMARKS:
        m_hwndStatic.DeleteProps(m_hwndStatic);
        return 0;
    case LNEM_SETLINEDELTA:
        m_hwndStatic.m_linedelta = INT(wParam);
        if (m_bSetRedraw)
            m_hwndStatic.Redraw();
        return 0;
    case LNEM_SETCOLUMNWIDTH:
        m_cxColumn = INT(wParam);
        Prepare();
        return 0;
    case LNEM_GETCOLUMNWIDTH:
        return m_cxColumn;
    case LNEM_GETLINEMARK:
        {
            LPCTSTR pszName = m_hwndStatic.GetPropName(INT(wParam));
            HANDLE hData = ::GetProp(m_hwndStatic, pszName);
            if (hData == NULL)
                return CLR_INVALID;
            COLORREF rgb = COLORREF(reinterpret_cast<ULONG_PTR>(hData));
            rgb &= 0x00FFFFFF;
            return rgb;
        }
    case WM_SETTEXT: case WM_CHAR: case WM_KEYDOWN: case WM_KEYUP:
    case WM_CUT: case WM_PASTE: case WM_UNDO:
    case EM_UNDO: case EM_REPLACESEL: case EM_SETHANDLE:
        // These may change the text (WM_KEYDOWN/UP are included because
        // e.g. the Delete key modifies text without a WM_CHAR), so the
        // static's cached copy of the text must be invalidated.
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        m_hwndStatic.InvalidateTextCache();
        if (m_bSetRedraw)
            m_hwndStatic.Redraw();
        return ret;
    case WM_VSCROLL:
    case EM_SCROLL: case EM_SCROLLCARET: case EM_LINESCROLL:
        // Pure scrolling/caret-visibility messages: the text itself is
        // unchanged, so just repaint and let the static reuse its cached
        // text/line-count instead of re-copying and re-scanning the whole
        // document. This matters a lot for mouse-wheel scrolling, which can
        // fire many of these messages back-to-back.
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        if (m_bSetRedraw)
            m_hwndStatic.Redraw();
        return ret;
    case WM_MOUSEWHEEL:
        if (GetKeyState(VK_CONTROL) < 0)
        {
            UINT id = GetDlgCtrlID(hwnd);
            if ((SHORT)HIWORD(wParam) < 0)
                PostMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(id, LNEN_ZOOMOUT), (LPARAM)hwnd);
            else
                PostMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(id, LNEN_ZOOMIN), (LPARAM)hwnd);
            return 0;
        }
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        if (m_bSetRedraw)
            m_hwndStatic.Redraw();
        return ret;
    case WM_SIZE: case WM_SETFONT:
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        m_cxColumn = 0; // clear cache
        Prepare();
        return ret;
    case EM_SETMARGINS:
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        m_cxColumn = 0; // clear cache
        Prepare();
        return ret;
    case WM_SETREDRAW:
        ret = DefWndProc(hwnd, uMsg, wParam, lParam);
        if (m_hwndStatic)
            ::SendMessage(m_hwndStatic, WM_SETREDRAW, wParam, lParam);
        m_bSetRedraw = (BOOL)wParam;
        return ret;
    }
    return DefWndProc(hwnd, uMsg, wParam, lParam);
}

LRESULT CALLBACK
LineNumEdit::SuperclassWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    LineNumEdit* pCtrl = NULL;
    if (uMsg == WM_NCCREATE)
    {
        pCtrl = new LineNumEdit();
        pCtrl->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pCtrl);
        pCtrl->m_fnOldWndProc = SuperclassWindow();
    }
    else if (uMsg == WM_NCDESTROY)
    {
        pCtrl = reinterpret_cast<LineNumEdit*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    LRESULT ret = LineNumBase::WindowProc(hwnd, uMsg, wParam, lParam);
    if (uMsg == WM_NCDESTROY)
        delete pCtrl;
    return ret;
}

INT LineNumEdit::GetColumnWidth()
{
    if (m_cxColumn)
        return m_cxColumn; // cached

    // get text extent
    SIZE siz;
    HDC hDC = ::GetDC(m_hwnd);
    HGDIOBJ hFontOld = ::SelectObject(hDC, GetWindowFont(m_hwnd));
    ::GetTextExtentPoint32(hDC, TEXT("0"), 1, &siz);
    ::SelectObject(hDC, hFontOld);
    ::ReleaseDC(m_hwnd, hDC);

    // get margins
    DWORD dwMargins = DWORD(::SendMessage(m_hwnd, EM_GETMARGINS, 0, 0));
    INT leftmargin = LOWORD(dwMargins), rightmargin = HIWORD(dwMargins);

    // save and return
    m_cxColumn = leftmargin + (m_num_digits * siz.cx) + rightmargin + leftmargin;
    return m_cxColumn;
}

WNDPROC LineNumEdit::SuperclassWindow() // "superclassing"
{
    static WNDPROC s_fnOldWndProc = NULL;
    if (s_fnOldWndProc)
        return s_fnOldWndProc;

    WNDCLASSEX wcx = { sizeof(wcx) };
    if (!::GetClassInfoEx(NULL, TEXT("EDIT"), &wcx))
        return NULL;

    s_fnOldWndProc = wcx.lpfnWndProc;
    wcx.lpszClassName = SuperWndClassName();
    wcx.lpfnWndProc = SuperclassWndProc;
    if (::RegisterClassEx(&wcx))
        return s_fnOldWndProc;
    return NULL;
}

/////////////////////////////////////////////////////////////////////////////////////////
// DllMain --- entry point of the DLL file

#ifdef LINENUMEDIT_DLL
    BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
    {
        switch (fdwReason)
        {
        case DLL_PROCESS_ATTACH:
            return (LineNumEdit::SuperclassWindow() != NULL);
        case DLL_PROCESS_DETACH:
            ::UnregisterClass(LineNumEdit::SuperWndClassName(), NULL);
            break;
        }
        return TRUE;
    }
#endif
