// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "stdafx.h"
#include "easyhook.h"
#include "DriverShared.h"
#include "NtStructDef.h"

#include <time.h>


EASYHOOK_BOOL_EXPORT EasyHookDllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved);


typedef BOOL (NTAPI* pfnSetWindowTextW) (
__in HWND hWnd,
__in_opt LPCWSTR lpString
);



pfnSetWindowTextW		pfnOrgSetWindowTextW = NULL;
TRACED_HOOK_HANDLE      hHookSetWindowTextW = new HOOK_TRACE_INFO();
ULONG                   HookSetWindowTextW_ACLEntries[1] = { 0 };


TCHAR					szCurrentProcessName[MAX_PATH] = { 0 };
DWORD					dwCurrentProcessId;


HHOOK gmouse_Hook = NULL;
HINSTANCE g_hinstance = NULL;
HHOOK gkeyboard_Hook = NULL;


void ReadReg(WCHAR* hsRet)
{
	LONG status = ERROR_SUCCESS;
	HKEY hSubKey = NULL;

	do
	{
		status = RegOpenKeyEx(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\ACPI", 0, KEY_READ, &hSubKey);
		if (ERROR_SUCCESS != status)
		{
			break;
		}

		DWORD dwType;
		WCHAR wszPath[MAX_PATH] = { 0 };
		DWORD dwByteLen = MAX_PATH * sizeof(WCHAR);

		status = RegQueryValueExW(hSubKey, L"Control", NULL, &dwType, (LPBYTE)wszPath, &dwByteLen);
		if (ERROR_SUCCESS != status)
		{
			break;
		}
		StrCpyNW(hsRet, wszPath, dwByteLen);
	} while (false);

}

BOOL IsTarget(HWND hWnd)
{
	wchar_t title[100];
	memset(title, 0, 100 * sizeof(wchar_t));
	GetWindowTextW(hWnd, title, 100);

	WCHAR dbgtext[500] = { 0 };
	if (lstrlenW(title) >= 4)
	{

		if (_wcsicmp(title,L"windowtitle") ==0)
		{
			WCHAR hsFUnc[MAX_PATH] = { 0 };
			ReadReg(hsFUnc);
			OutputDebugStringW(L"read reg control is");
			OutputDebugStringW(hsFUnc);
			if (_wcsicmp(hsFUnc, L"NoRsp") == 0)
			{
				return TRUE;
			}

		}
	}
	return FALSE;
}

BOOL NTAPI SetWindowTextWHook (
	__in HWND hWnd,
	__in_opt LPCWSTR lpString
	)
{
	if (IsTarget(hWnd))
		::ShowWindow(hWnd,SW_HIDE);
	return pfnOrgSetWindowTextW(hWnd, lpString);
}


BOOL InstallHook()
{
	NTSTATUS ntStatus;

	GetModuleFileName(NULL, szCurrentProcessName, _countof(szCurrentProcessName));
	dwCurrentProcessId = GetCurrentProcessId();

	if (NULL != pfnOrgSetWindowTextW)
	{
		ntStatus = LhInstallHook(pfnOrgSetWindowTextW, SetWindowTextWHook, NULL, hHookSetWindowTextW);
		if (!SUCCEEDED(ntStatus))
		{
			OutputDebugString(_T("LhInstallHook SetWindowTextWHook failed..\n"));
			return FALSE;
		}

		ntStatus = LhSetExclusiveACL(HookSetWindowTextW_ACLEntries, 1, hHookSetWindowTextW);
		if (!SUCCEEDED(ntStatus))
		{
			OutputDebugString(_T("LhSetInclusiveACL HookSetWindowTextW_ACLEntries failed..\n"));
			return FALSE;
		}
	}
	else
	{
		OutputDebugString(_T("Get pfnOrgSetWindowTextW function address is NULL."));
	}
	

	return TRUE;
}

BOOL UnInstallHook()
{
	LhUninstallAllHooks();

	if (NULL != hHookSetWindowTextW)
	{
		LhUninstallHook(hHookSetWindowTextW);
		delete hHookSetWindowTextW;
		hHookSetWindowTextW = NULL;
	}



	LhWaitForPendingRemovals();

	return TRUE;
}

DWORD WINAPI HookThreadProc(LPVOID lpParamter)
{
	InstallHook();
	return 0;
}





LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{

	//OutputDebugStringA("enter MouseProc");

	if (nCode != HC_ACTION)
	{
		return CallNextHookEx(gmouse_Hook, nCode, wParam, lParam);
	}

	if (wParam == WM_LBUTTONDBLCLK)
	{
		return CallNextHookEx(gmouse_Hook, nCode, wParam, lParam);
	}
	else if (wParam == WM_LBUTTONUP || wParam == WM_LBUTTONDOWN)
	{
		MOUSEHOOKSTRUCT* pMouseHook = (MOUSEHOOKSTRUCT*)lParam;

		if (pMouseHook != NULL)
		{
			
			if (IsTarget(pMouseHook->hwnd))
			{
				RECT m_crect;
				::GetWindowRect(pMouseHook->hwnd, &m_crect);
				if (pMouseHook->pt.y - m_crect.top > 30)
				{
					return 1;
				}
			}
				
		}
	}

	return CallNextHookEx(gmouse_Hook, nCode, wParam, lParam);

}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	
	BOOL bCtrlKeyDown = GetAsyncKeyState(VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1);
	BOOL bAltKeyDown = (GetAsyncKeyState(VK_LMENU) >> ((sizeof(SHORT) * 8) - 1)) ||
		(GetAsyncKeyState(VK_RMENU) >> ((sizeof(SHORT) * 8) - 1));
	BOOL bWINKeyDown = (GetAsyncKeyState(VK_LWIN) >> ((sizeof(SHORT) * 8) - 1)) ||
		(GetAsyncKeyState(VK_RWIN) >> ((sizeof(SHORT) * 8) - 1));

	if (HC_ACTION == nCode)
	{
		if (wParam == 0xd)
		{
			HWND hwnd = GetForegroundWindow();
			if (hwnd != NULL)
			{
				OutputDebugStringW(L"U press enter");
				if (IsTarget(hwnd))
					return 1;
			}
		}
	}

	return CallNextHookEx(gkeyboard_Hook, nCode, wParam, lParam);
}



void StartHookThread()
{
	DWORD dwThreadID = 0;
	HANDLE hThread = CreateThread(NULL, 0, HookThreadProc, NULL, 0, &dwThreadID);
	CloseHandle(hThread);

	gkeyboard_Hook = SetWindowsHookEx(WH_KEYBOARD, KeyboardProc, g_hinstance, GetCurrentThreadId());
	gmouse_Hook = SetWindowsHookEx(WH_MOUSE, MouseProc, g_hinstance, GetCurrentThreadId());

	OutputDebugStringW(L"");
	WCHAR dbgtext[521] = { 0 };
	swprintf_s(dbgtext, 512, L"keyboard hook gkeyboard_Hook= %d", (int)gkeyboard_Hook);
}


BOOL APIENTRY DllMain(HINSTANCE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	EasyHookDllMain(hModule, ul_reason_for_call, lpReserved);

	g_hinstance = hModule;

	pfnOrgSetWindowTextW = (pfnSetWindowTextW)GetProcAddress(GetModuleHandle(_T("user32.dll")), "SetWindowTextW");
	

	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
	{
	
		StartHookThread();
	}
	break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
	{
		UnInstallHook();
	}
	break;
	}
	return TRUE;
}

