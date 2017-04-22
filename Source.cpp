#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "wininet")
#pragma comment(lib, "comctl32")

#include <windows.h>
#include <commctrl.h>
#include <wininet.h>
#include <string>
#include <vector>
#include "resource.h"

std::wstring trim(const std::wstring& string, LPCWSTR trimCharacterList = L" \"\t\v\r\n")
{
	std::wstring result;
	std::wstring::size_type left = string.find_first_not_of(trimCharacterList);
	if (left != std::wstring::npos) {
		std::wstring::size_type right = string.find_last_not_of(trimCharacterList);
		result = string.substr(left, right - left + 1);
	}
	return result;
}

BOOL GetValueFromJSON(LPCWSTR lpszJson, LPCWSTR lpszKey, LPWSTR lpszValue)
{
	std::wstring json(lpszJson);
	std::wstring key(lpszKey);
	key = L"\"" + key + L"\"";
	size_t posStart = json.find(key);
	if (posStart == std::wstring::npos) return FALSE;
	posStart += key.length();
	posStart = json.find(L':', posStart);
	if (posStart == std::wstring::npos) return FALSE;
	++posStart;
	size_t posEnd = json.find(L',', posStart);
	if (posEnd == std::wstring::npos) {
		posEnd = json.find(L'}', posStart);
		if (posEnd == std::wstring::npos) return FALSE;
	}
	std::wstring value(json, posStart, posEnd - posStart);
	value = trim(value);
	lstrcpyW(lpszValue, value.c_str());
	return TRUE;
}

DWORD GetHttpHeaderStatusCode(HINTERNET hHttpRequest)
{
	DWORD dwStatusCode = 0;
	DWORD dwLength = sizeof(DWORD);
	HttpQueryInfo(hHttpRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwLength, 0);
	return dwStatusCode;
}

LPWSTR Post(LPCWSTR lpszServer, LPCWSTR lpszPath, LPCWSTR lpszData)
{
	LPWSTR result = 0;
	LPCWSTR hdrs = L"Content-Type: application/x-www-form-urlencoded";
	const HINTERNET hInternet = InternetOpenW(L"WinInet Toot Program", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
	if (hInternet == 0) goto END1;
	const HINTERNET hHttpSession = InternetConnectW(hInternet, lpszServer, INTERNET_DEFAULT_HTTPS_PORT, 0, 0, INTERNET_SERVICE_HTTP, 0, 0);
	if (!hHttpSession) goto END2;
	const HINTERNET hHttpRequest = HttpOpenRequestW(hHttpSession, L"POST", lpszPath, 0, 0, 0, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
	if (!hHttpRequest) goto END3;
	DWORD dwTextLen = WideCharToMultiByte(CP_UTF8, 0, lpszData, -1, 0, 0, 0, 0);
	LPSTR lpszDataA = (LPSTR)GlobalAlloc(GPTR, dwTextLen);
	WideCharToMultiByte(CP_UTF8, 0, lpszData, -1, lpszDataA, dwTextLen, 0, 0);
	if (HttpSendRequestW(hHttpRequest, hdrs, lstrlenW(hdrs), lpszDataA, lstrlenA(lpszDataA)) == FALSE) goto END4;
	if (GetHttpHeaderStatusCode(hHttpRequest) != HTTP_STATUS_OK)  goto END4;
	{
		LPBYTE lpszByte = (LPBYTE)GlobalAlloc(GPTR, 1);
		DWORD dwRead, dwSize = 0;
		static BYTE szBuffer[1024 * 4];
		for (;;) {
			if (!InternetReadFile(hHttpRequest, szBuffer, (DWORD)sizeof(szBuffer), &dwRead) || !dwRead) break;
			LPBYTE lpTemp = (LPBYTE)GlobalReAlloc(lpszByte, (SIZE_T)(dwSize + dwRead + 1), GMEM_MOVEABLE);
			if (lpTemp == 0) break;
			lpszByte = lpTemp;
			CopyMemory(lpszByte + dwSize, szBuffer, dwRead);
			dwSize += dwRead;
		}
		lpszByte[dwSize] = 0;
		if (lpszByte[0]) {
			dwTextLen = MultiByteToWideChar(CP_UTF8, 0, (LPSTR)lpszByte, -1, 0, 0);
			result = (LPWSTR)GlobalAlloc(GPTR, dwTextLen * sizeof(WCHAR));
			MultiByteToWideChar(CP_UTF8, 0, (LPSTR)lpszByte, -1, result, dwTextLen);
		}
		GlobalFree(lpszByte);
	}
END4:
	GlobalFree(lpszDataA);
	InternetCloseHandle(hHttpRequest);
END3:
	InternetCloseHandle(hHttpSession);
END2:
	InternetCloseHandle(hInternet);
END1:
	return result;
}

BOOL GetHttpHeaderLinkUrl(HINTERNET hHttpRequest, LPWSTR lpszLinkNext, LPWSTR lpszLinkPrev)
{
	DWORD dwSize = sizeof(WCHAR) * 20;
	LPWSTR lpOutBuffer = (LPWSTR)GlobalAlloc(0, dwSize);
	LPCWSTR lpszName = L"Link";
	lstrcpyW(lpOutBuffer, lpszName);
retry:
	if (!HttpQueryInfoW(hHttpRequest, HTTP_QUERY_CUSTOM, (LPVOID)lpOutBuffer, &dwSize, 0)) {
		if (GetLastError() == ERROR_HTTP_HEADER_NOT_FOUND) {
			GlobalFree(lpOutBuffer);
			return FALSE;
		} else {
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
				GlobalFree(lpOutBuffer);
				lpOutBuffer = (LPWSTR)GlobalAlloc(0, dwSize);
				lstrcpyW(lpOutBuffer, lpszName);
				goto retry;
			} else {
				GlobalFree(lpOutBuffer);
				return FALSE;
			}
		}
	}
	int count = 0;
	LPWSTR p = lpOutBuffer, pStart, pEnd;
	while (*p) {
		if (*p == L'<') {
			pStart = p;
		}
		if (*p == L'>') {
			pEnd = p;
			lstrcpynW(count == 0 ? lpszLinkNext : lpszLinkPrev, pStart + 1, (int)(pEnd - pStart));
			if (count == 1) break;
			++count;
		}
		++p;
	}
	GlobalFree(lpOutBuffer);
	return TRUE;
}

LPWSTR Get(LPCWSTR lpszServer, LPCWSTR lpszPath, LPCWSTR lpszData, LPWSTR lpszLinkNext, LPWSTR lpszLinkPrev)
{
	LPWSTR result = 0;
	LPCWSTR hdrs = L"Content-Type: application/x-www-form-urlencoded";
	const HINTERNET hInternet = InternetOpenW(L"WinInet Toot Program", INTERNET_OPEN_TYPE_PRECONFIG, 0, 0, 0);
	if (hInternet == 0) goto END1;
	const HINTERNET hHttpSession = InternetConnectW(hInternet, lpszServer, INTERNET_DEFAULT_HTTPS_PORT, 0, 0, INTERNET_SERVICE_HTTP, 0, 0);
	if (!hHttpSession) goto END2;
	const HINTERNET hHttpRequest = HttpOpenRequestW(hHttpSession, L"GET", lpszPath, 0, 0, 0, INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD, 0);
	if (!hHttpRequest) goto END3;
	DWORD dwTextLen = WideCharToMultiByte(CP_UTF8, 0, lpszData, -1, 0, 0, 0, 0);
	LPSTR lpszDataA = (LPSTR)GlobalAlloc(GPTR, dwTextLen);
	WideCharToMultiByte(CP_UTF8, 0, lpszData, -1, lpszDataA, dwTextLen, 0, 0);
	if (HttpSendRequestW(hHttpRequest, hdrs, lstrlenW(hdrs), lpszDataA, lstrlenA(lpszDataA)) == FALSE) goto END4;
	if (GetHttpHeaderStatusCode(hHttpRequest) != HTTP_STATUS_OK)  goto END4;
	if (lpszLinkNext || lpszLinkPrev) {
		GetHttpHeaderLinkUrl(hHttpRequest, lpszLinkNext, lpszLinkPrev);
	}
	{
		LPBYTE lpszByte = (LPBYTE)GlobalAlloc(GPTR, 1);
		DWORD dwRead, dwSize = 0;
		static BYTE szBuffer[1024 * 4];
		for (;;) {
			if (!InternetReadFile(hHttpRequest, szBuffer, (DWORD)sizeof(szBuffer), &dwRead) || !dwRead) break;
			LPBYTE lpTemp = (LPBYTE)GlobalReAlloc(lpszByte, (SIZE_T)(dwSize + dwRead + 1), GMEM_MOVEABLE);
			if (lpTemp == 0) break;
			lpszByte = lpTemp;
			CopyMemory(lpszByte + dwSize, szBuffer, dwRead);
			dwSize += dwRead;
		}
		lpszByte[dwSize] = 0;
		if (lpszByte[0]) {
			dwTextLen = MultiByteToWideChar(CP_UTF8, 0, (LPSTR)lpszByte, -1, 0, 0);
			result = (LPWSTR)GlobalAlloc(GPTR, dwTextLen * sizeof(WCHAR));
			MultiByteToWideChar(CP_UTF8, 0, (LPSTR)lpszByte, -1, result, dwTextLen);
		}
		GlobalFree(lpszByte);
	}
END4:
	InternetCloseHandle(hHttpRequest);
END3:
	InternetCloseHandle(hHttpSession);
END2:
	InternetCloseHandle(hInternet);
END1:
	return result;
}

BOOL GetClientIDAndClientSecret(HWND hEditOutput, LPCWSTR lpszServer, LPWSTR lpszID, LPWSTR lpszSecret)
{
	BOOL result = FALSE;
	WCHAR szData[1024];
	lstrcpyW(szData, L"client_name=TootApp&redirect_uris=urn:ietf:wg:oauth:2.0:oob&scopes=read%20write%20follow");
	LPWSTR lpszReturn = Post(lpszServer, L"/api/v1/apps", szData);
	if (lpszReturn) {
		SendMessageW(hEditOutput, EM_REPLACESEL, 0, (LPARAM)lpszReturn);
		SendMessageW(hEditOutput, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
		result = GetValueFromJSON(lpszReturn, L"client_id", lpszID) & GetValueFromJSON(lpszReturn, L"client_secret", lpszSecret);
		GlobalFree(lpszReturn);
	}
	return result;
}

BOOL GetAccessToken(HWND hEditOutput, LPCWSTR lpszServer, LPCWSTR lpszID, LPCWSTR lpszSecret, LPCWSTR lpszUserName, LPCWSTR lpszPassword, LPWSTR lpszAccessToken)
{
	BOOL result = FALSE;
	WCHAR szData[1024];
	wsprintfW(szData, L"scope=read write follow&client_id=%s&client_secret=%s&grant_type=password&username=%s&password=%s", lpszID, lpszSecret, lpszUserName, lpszPassword);
	LPWSTR lpszReturn = Post(lpszServer, L"/oauth/token", szData);
	if (lpszReturn) {
		SendMessageW(hEditOutput, EM_REPLACESEL, 0, (LPARAM)lpszReturn);
		SendMessageW(hEditOutput, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
		result = GetValueFromJSON(lpszReturn, L"access_token", lpszAccessToken);
		GlobalFree(lpszReturn);
	}
	return result;
}

BOOL GetCurrentUserID(HWND hEditOutput, LPCWSTR lpszServer, LPCWSTR lpszAccessToken, int* pnUserID)
{
	BOOL result = FALSE;
	WCHAR szPath[1024];
	wsprintfW(szPath, L"/api/v1/accounts/verify_credentials");
	WCHAR szData[1024];
	wsprintfW(szData, L"access_token=%s", lpszAccessToken);
	LPWSTR lpszReturn = Get(lpszServer, szPath, szData, 0, 0);
	if (lpszReturn) {
		SendMessageW(hEditOutput, EM_REPLACESEL, 0, (LPARAM)lpszReturn);
		SendMessageW(hEditOutput, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
		TCHAR szUserID[16];
		if (GetValueFromJSON(lpszReturn, L"id", szUserID)) {
			*pnUserID = _wtol(szUserID);
			if (*pnUserID != 0)
				result = TRUE;
		}
		GlobalFree(lpszReturn);
	}
	return result;
}

typedef struct {
	int nUserID;
	WCHAR szUserName[256];
} ACCOUNT;

BOOL GetFollowList(HWND hEditOutput, LPCWSTR lpszServer, LPCWSTR lpszAccessToken, int nUserID, BOOL bFollowingOrFollowers, std::vector<ACCOUNT*> & list)
{
	list.clear();
	WCHAR szPath[64];
	wsprintfW(szPath, bFollowingOrFollowers ? L"/api/v1/accounts/%d/following" : L"/api/v1/accounts/%d/followers", nUserID);
	WCHAR szData[256];
	WCHAR szLinkNext[256] = { 0 };
	WCHAR szLinkPrev[256] = { 0 };
	int nLastMaxID = -1;
	for (int i = 0; i < 100; ++i) {
		if (i == 0) {
			wsprintfW(szData, L"access_token=%s&limit=80", lpszAccessToken);
		} else if (szLinkNext[0] == 0) {
			break;
		} else {
			LPCWSTR lpszMaxID = L"max_id=";
			LPWSTR pStart = wcsstr(szLinkNext, lpszMaxID);
			if (!pStart) break;
			pStart += lstrlen(lpszMaxID);
			const int nMaxID = _wtol(pStart);
			if (nMaxID == 0 || nMaxID == nLastMaxID) break;
			wsprintfW(szData, L"access_token=%s&limit=80&max_id=%d", lpszAccessToken, nMaxID);
			nLastMaxID = nMaxID;
		}
		LPWSTR lpszReturn = Get(lpszServer, szPath, szData, szLinkNext, szLinkPrev);
		if (lpszReturn) {
			SendMessageW(hEditOutput, EM_REPLACESEL, 0, (LPARAM)lpszReturn);
			SendMessageW(hEditOutput, EM_REPLACESEL, 0, (LPARAM)L"\r\n");
			WCHAR szUserID[16];
			std::wstring jsonlist(lpszReturn);
			size_t posStart = 0;
			size_t posEnd = 0;
			int i = 0;
			for (;;) {
				posStart = jsonlist.find(L"{", posStart);
				if (posStart == std::wstring::npos) break;
				posEnd = jsonlist.find(L"},", posStart + 1);
				if (posEnd == std::wstring::npos) {
					posEnd = jsonlist.find(L"}]", posStart + 1);
					if (posEnd == std::wstring::npos) break;
				}
				++posEnd;
				std::wstring value(jsonlist, posStart, posEnd - posStart);
				ACCOUNT *account = new ACCOUNT;
				if (GetValueFromJSON(value.c_str(), L"id", szUserID) & GetValueFromJSON(value.c_str(), L"username", account->szUserName)) {
					account->nUserID = _wtol(szUserID);
					list.push_back(account);
				} else {
					delete account;
				}
				posStart = posEnd;
			}
			GlobalFree(lpszReturn);
		}
	}
	return list.size() > 0;
}

class EditBox
{
	WNDPROC fnWndProc;
	LPTSTR m_lpszPlaceholder;
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		EditBox* _this = (EditBox*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (_this) {
			if (msg == WM_PAINT) {
				const LRESULT lResult = CallWindowProc(_this->fnWndProc, hWnd, msg, wParam, lParam);
				const int nTextLength = GetWindowTextLength(hWnd);
				if ((_this->m_lpszPlaceholder && !nTextLength)) {
					const HDC hdc = GetDC(hWnd);
					const COLORREF OldColor = SetTextColor(hdc, RGB(180, 180, 180));
					const HFONT hOldFont = (HFONT)SelectObject(hdc, (HFONT)SendMessage(hWnd, WM_GETFONT, 0, 0));
					const int nLeft = LOWORD(SendMessage(hWnd, EM_GETMARGINS, 0, 0));
					if (_this->m_lpszPlaceholder && !nTextLength) {
						TextOut(hdc, nLeft + 4, 2, _this->m_lpszPlaceholder, lstrlen(_this->m_lpszPlaceholder));
					}
					SelectObject(hdc, hOldFont);
					SetTextColor(hdc, OldColor);
					ReleaseDC(hWnd, hdc);
				}
				return lResult;
			}
			else if (msg == WM_CHAR && wParam == 1) {
				SendMessage(hWnd, EM_SETSEL, 0, -1);
				return 0;
			}
			return CallWindowProc(_this->fnWndProc, hWnd, msg, wParam, lParam);
		}
		return 0;
	}
public:
	HWND m_hWnd;
	EditBox(LPCTSTR lpszDefaultText, DWORD dwStyle, int x, int y, int width, int height, HWND hParent, HMENU hMenu, LPCTSTR lpszPlaceholder)
		: m_hWnd(0), fnWndProc(0), m_lpszPlaceholder(0) {
		if (lpszPlaceholder && !m_lpszPlaceholder) {
			m_lpszPlaceholder = (LPTSTR)GlobalAlloc(0, sizeof(TCHAR) * (lstrlen(lpszPlaceholder) + 1));
			lstrcpy(m_lpszPlaceholder, lpszPlaceholder);
		}
		m_hWnd = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), lpszDefaultText, dwStyle, x, y, width, height, hParent, hMenu, GetModuleHandle(0), 0);
		SetWindowLongPtr(m_hWnd, GWLP_USERDATA, (LONG_PTR)this);
		fnWndProc = (WNDPROC)SetWindowLongPtr(m_hWnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
	}
	~EditBox() { DestroyWindow(m_hWnd); GlobalFree(m_lpszPlaceholder); }
};

HHOOK g_hHook;
LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	if (nCode == HCBT_ACTIVATE) {
		UnhookWindowsHookEx(g_hHook);
		const HWND hMessageBox = (HWND)wParam;
		const HWND hParentWnd = GetParent(hMessageBox);
		RECT rectMessageBox, rectParentWnd;
		GetWindowRect(hMessageBox, &rectMessageBox);
		GetWindowRect(hParentWnd, &rectParentWnd);
		SetWindowPos(hMessageBox, hParentWnd,
			(rectParentWnd.right + rectParentWnd.left - rectMessageBox.right + rectMessageBox.left) >> 1,
			(rectParentWnd.bottom + rectParentWnd.top - rectMessageBox.bottom + rectMessageBox.top) >> 1,
			0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}
	return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static EditBox *pEdit1, *pEdit2, *pEdit3;
	static HWND hEdit5, hButton, hList1, hList2;
	static HFONT hFont;
	static WCHAR szAccessToken[65];
	static BOOL bModified;
	static std::vector<ACCOUNT*> listFollowing;
	static std::vector<ACCOUNT*> listFollowers;
	switch (msg) {
	case WM_CREATE:
		InitCommonControls();
		hFont = CreateFont(24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, TEXT("Yu Gothic UI"));
		pEdit1 = new EditBox(0, WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, 0, TEXT("サーバー名"));
		SendMessage(pEdit1->m_hWnd, WM_SETFONT, (WPARAM)hFont, 0);
		pEdit2 = new EditBox(0, WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, 0, TEXT("メールアドレス"));
		SendMessage(pEdit2->m_hWnd, WM_SETFONT, (WPARAM)hFont, 0);
		pEdit3 = new EditBox(0, WS_VISIBLE | WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL | ES_PASSWORD, 0, 0, 0, 0, hWnd, 0, TEXT("パスワード"));
		SendMessage(pEdit3->m_hWnd, WM_SETFONT, (WPARAM)hFont, 0);
		hButton = CreateWindow(TEXT("BUTTON"), TEXT("フォロー・フォロワーリストを取得"), WS_VISIBLE | WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0, 0, 0, hWnd, (HMENU)1000, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		SendMessage(hButton, WM_SETFONT, (WPARAM)hFont, 0);		
		hList1 = CreateWindowEx(WS_EX_CLIENTEDGE,
			WC_LISTVIEW, 0, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA,
			0, 0, 0, 0, hWnd, (HMENU)1001, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		ListView_SetExtendedListViewStyle(hList1, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
		SendMessage(hList1, WM_SETFONT, (WPARAM)hFont, 0);
		{
			LV_COLUMN lvcolumn = { 0 };
			lvcolumn.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
			lvcolumn.cx = 256;
			lvcolumn.pszText = TEXT("フォロー");
			ListView_InsertColumn(hList1, 0, &lvcolumn);
		}
		hList2 = CreateWindowEx(WS_EX_CLIENTEDGE,
			WC_LISTVIEW, 0, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA,
			0, 0, 0, 0, hWnd, (HMENU)1002, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		ListView_SetExtendedListViewStyle(hList2, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
		SendMessage(hList2, WM_SETFONT, (WPARAM)hFont, 0);
		{
			LV_COLUMN lvcolumn = { 0 };
			lvcolumn.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
			lvcolumn.cx = 256;
			lvcolumn.pszText = TEXT("フォロワー");
			ListView_InsertColumn(hList2, 0, &lvcolumn);
		}
		hEdit5 = CreateWindowEx(WS_EX_CLIENTEDGE, TEXT("EDIT"), 0, WS_VISIBLE | WS_CHILD | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_READONLY, 0, 0, 0, 0, hWnd, 0, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		SendMessage(hEdit5, WM_SETFONT, (WPARAM)hFont, 0);
		SendMessage(hEdit5, EM_LIMITTEXT, 0, 0);
		break;
	case WM_SIZE:
		MoveWindow(pEdit1->m_hWnd, 10, 10, LOWORD(lParam) - 20, 32, TRUE);
		MoveWindow(pEdit2->m_hWnd, 10, 50, LOWORD(lParam) - 20, 32, TRUE);
		MoveWindow(pEdit3->m_hWnd, 10, 90, LOWORD(lParam) - 20, 32, TRUE);
		MoveWindow(hButton, 10, 130, LOWORD(lParam) - 20, 32, TRUE);
		MoveWindow(hList1, 10, 170, LOWORD(lParam) / 2 - 15, HIWORD(lParam) - 170 - 90 - 20, TRUE);
		MoveWindow(hList2, LOWORD(lParam) / 2 + 5, 170, LOWORD(lParam) / 2 - 15, HIWORD(lParam) - 170 - 90 - 20, TRUE);
		MoveWindow(hEdit5, 10, HIWORD(lParam) - 100, LOWORD(lParam) - 20, 90, TRUE);
		break;
	case WM_NOTIFY:
		if ((int)wParam == 1001 || (int)wParam == 1002) {
			LV_DISPINFO *pLvDispInfo = (LV_DISPINFO *)lParam;
			if (pLvDispInfo->hdr.code == LVN_GETDISPINFO) {
				TCHAR szText[MAX_PATH];
				if (pLvDispInfo->item.iSubItem == 0) {
					if (pLvDispInfo->item.mask&LVIF_TEXT) {
						if ((int)wParam == 1001) {
							wsprintf(szText, TEXT("%s(%d)"),
								listFollowing.at(pLvDispInfo->item.iItem)->szUserName,
								listFollowing.at(pLvDispInfo->item.iItem)->nUserID
								);
						} else if ((int)wParam == 1002) {
							wsprintf(szText, TEXT("%s(%d)"),
								listFollowers.at(pLvDispInfo->item.iItem)->szUserName,
								listFollowers.at(pLvDispInfo->item.iItem)->nUserID
							);
						}
						if (lstrlen(szText)<pLvDispInfo->item.cchTextMax) {
							lstrcpy(pLvDispInfo->item.pszText, szText);
						} else {
							pLvDispInfo->item.pszText[0] = 0;
						}
					}
				}
				return TRUE;
			}
		}
		break;
	case WM_COMMAND:
		if (HIWORD(wParam) == EN_CHANGE) {
			InvalidateRect((HWND)lParam, 0, 0);
			bModified = TRUE;
		}
		else if (LOWORD(wParam) == 1000) {
			SetWindowText(hEdit5, 0);
			WCHAR szServer[256] = { 0 };
			GetWindowTextW(pEdit1->m_hWnd, szServer, _countof(szServer));
			URL_COMPONENTSW uc = { sizeof(uc) };
			WCHAR szHostName[256] = { 0 };
			uc.lpszHostName = szHostName;
			uc.dwHostNameLength = _countof(szHostName);
			if (InternetCrackUrlW(szServer, 0, 0, &uc)) {
				lstrcpyW(szServer, szHostName);
			}
			if (bModified || !lstrlen(szAccessToken)) {
				InternetSetOption(0, INTERNET_OPTION_END_BROWSER_SESSION, 0, 0);
				static WCHAR szClientID[65] = { 0 };
				static WCHAR szSecret[65] = { 0 };
				if (!GetClientIDAndClientSecret(hEdit5, szServer, szClientID, szSecret)) return 0;
				WCHAR szUserName[256] = { 0 };
				GetWindowTextW(pEdit2->m_hWnd, szUserName, _countof(szUserName));
				WCHAR szPassword[256] = { 0 };
				GetWindowTextW(pEdit3->m_hWnd, szPassword, _countof(szPassword));
				if (!GetAccessToken(hEdit5, szServer, szClientID, szSecret, szUserName, szPassword, szAccessToken)) return 0;
				bModified = FALSE;
			}
			int nUserID = 0;
			if (!GetCurrentUserID(hEdit5, szServer, szAccessToken, &nUserID)) return 0;
			GetFollowList(hEdit5, szServer, szAccessToken, nUserID, TRUE, listFollowing);
			{
				int nCount = (int)listFollowing.size();
				WCHAR szText[256];
				wsprintf(szText, TEXT("フォロー(%d)"), nCount);
				LV_COLUMN lvcolumn = { 0 };
				lvcolumn.mask = LVCF_TEXT | LVCF_SUBITEM;
				lvcolumn.pszText = szText;
				ListView_SetColumn(hList1, 0, &lvcolumn);
				ListView_SetItemCountEx(hList1, listFollowing.size(), LVSICF_NOINVALIDATEALL);
			}
			GetFollowList(hEdit5, szServer, szAccessToken, nUserID, FALSE, listFollowers);
			{
				int nCount = (int)listFollowers.size();
				WCHAR szText[256];
				wsprintf(szText, TEXT("フォロワー(%d)"), nCount);
				LV_COLUMN lvcolumn = { 0 };
				lvcolumn.mask = LVCF_TEXT | LVCF_SUBITEM;
				lvcolumn.pszText = szText;
				ListView_SetColumn(hList2, 0, &lvcolumn);
				ListView_SetItemCountEx(hList2, listFollowers.size(), LVSICF_NOINVALIDATEALL);
			}
			WCHAR szResult[1024];
			wsprintfW(szResult, L"取得しました。");
			g_hHook = SetWindowsHookEx(WH_CBT, CBTProc, 0, GetCurrentThreadId());
			MessageBoxW(hWnd, szResult, L"確認", 0);
		}
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		delete pEdit1;
		delete pEdit2;
		delete pEdit3;
		DeleteObject(hFont);
		PostQuitMessage(0);
		break;
	default:
		return DefDlgProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPreInst, LPSTR pCmdLine, int nCmdShow)
{
	LPCTSTR lpszClassName = TEXT("Window");
	MSG msg = { 0 };
	const WNDCLASS wndclass = { 0,WndProc,0,DLGWINDOWEXTRA,hInstance,LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1)),LoadCursor(0,IDC_ARROW),0,0,lpszClassName };
	RegisterClass(&wndclass);
	const HWND hWnd = CreateWindow(lpszClassName, TEXT("Mastodonのフォロー・フォロワーリストを取得"), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0, 800, 800, 0, 0, hInstance, 0);
	ShowWindow(hWnd, SW_SHOWDEFAULT);
	UpdateWindow(hWnd);
	while (GetMessage(&msg, 0, 0, 0)) {
		if (!IsDialogMessage(hWnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}