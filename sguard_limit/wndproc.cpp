#include <stdio.h>
#include "tray.h"
#include "config.h"
#include "tracecore.h"
#include "resource.h"

#include "wndproc.h"

extern HWND						g_hWnd;
extern HINSTANCE				g_hInstance;
extern volatile bool			g_bHijackThreadWaiting;

extern volatile DWORD			g_Mode;

extern volatile bool			limitEnabled;
extern volatile DWORD			limitPercent;

extern volatile bool			lockEnabled;
extern volatile DWORD			lockMode;
extern volatile lockedThreads_t	lockedThreads[3];
extern volatile DWORD			lockPid;
extern volatile DWORD			lockRound;


// about func: show about dialog box.
static void ShowAbout() {
	MessageBox(0,
		"本工具启动后自动优化后台ACE-Guard Client EXE的资源占用。\n"
		"该工具仅供研究交流游戏优化使用，将来可能失效，不保证稳定性。\n"
		"若您发现该工具已无法正常使用，请更换模式或选项；若还不行请停止使用，并将遇到的错误反馈至下方链接。\n\n"
		"工作模式说明：\n"
		"1 时间片轮转（旧模式）：已知可能导致“96-0”，若出现该情况可切换至【线程跟踪】。\n"
		"  (当然，即使没有出现错误你也可以使用新模式，不会有任何影响)\n\n"
		"2 线程追踪（新模式）：已知部分机器使用“锁定”选项时会出现“3009-0”，若出现该情况请直接切换到【锁定-rr】。\n"
		"如果你使用【锁定-rr】依旧出问题，可点击【设置时间切分】，并尝试较小的时间。例如尝试90，85，80...直到合适即可。\n"
		"注：【时间切分】设置的值越大，则约束等级越高；设置的值越小，则越稳定。\n\n"
		"论坛链接：https://bbs.colg.cn/thread-8087898-1-1.html \n"
		"项目地址：https://github.com/H3d9/sguard_limit （点ctrl+C复制到记事本）",
		"SGuard限制器 " VERSION "  colg@H3d9",
		MB_OK);
}

// disable func: undo functional control.
static void disableLimit() {
	limitEnabled = false;
	while (!g_bHijackThreadWaiting); // spin; wait till hijack release target thread.
}

static void disableLock() {
	lockEnabled = false;
	for (auto i = 0; i < 3; i++) {
		if (lockedThreads[i].locked) {
			ResumeThread(lockedThreads[i].handle);
			lockedThreads[i].locked = false;
		}
	}
}


// dialog: set time slice.
static INT_PTR CALLBACK SetTimeDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {

	switch (message) {
	case WM_INITDIALOG:
	{
		char buf[128];
		sprintf(buf, "输入1~99的整数（当前值：%d）", lockRound);
		SetDlgItemText(hDlg, IDC_SETTIMETEXT, buf);
			return (INT_PTR)TRUE;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDC_SETTIMEOK) {
			BOOL translated;
			UINT res = GetDlgItemInt(hDlg, IDC_SETTIMEEDIT, &translated, FALSE);
			if (!translated || res < 1 || res > 99) {
				MessageBox(0, "输入1~99的整数", "错误", MB_OK);
			} else {
				lockRound = res;
				EndDialog(hDlg, LOWORD(wParam));
				return (INT_PTR)TRUE;
			}
		}
		break;

	case WM_CLOSE:
		EndDialog(hDlg, LOWORD(wParam));
		return (INT_PTR)TRUE;
	}

	return (INT_PTR)FALSE;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_TRAYACTIVATE:
	{
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			HMENU hMenu = CreatePopupMenu();
			POINT pt;
			if (g_Mode == 0) {
				if (!limitEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 用户手动暂停");
				} else if (g_bHijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 等待游戏运行");
				} else {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 侦测到SGuard");
				}
				AppendMenu(hMenu, MFT_STRING, IDM_SWITCHMODE, "当前模式：时间片轮转 [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_PERCENT90, "限制资源：90%");
				AppendMenu(hMenu, MFT_STRING, IDM_PERCENT95, "限制资源：95%");
				AppendMenu(hMenu, MFT_STRING, IDM_PERCENT99, "限制资源：99%");
				AppendMenu(hMenu, MFT_STRING, IDM_PERCENT999, "限制资源：99.9%");
				AppendMenu(hMenu, MFT_STRING, IDM_STOPLIMIT, "停止限制");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
				if (limitEnabled) {
					switch (limitPercent) {
					case 90:
						CheckMenuItem(hMenu, IDM_PERCENT90, MF_CHECKED);
						break;
					case 95:
						CheckMenuItem(hMenu, IDM_PERCENT95, MF_CHECKED);
						break;
					case 99:
						CheckMenuItem(hMenu, IDM_PERCENT99, MF_CHECKED);
						break;
					case 999:
						CheckMenuItem(hMenu, IDM_PERCENT999, MF_CHECKED);
						break;
					}
				} else {
					CheckMenuItem(hMenu, IDM_STOPLIMIT, MF_CHECKED);
				}
			} else { // if g_Mode == 1
				if (!lockEnabled) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 用户手动暂停");
				} else if (g_bHijackThreadWaiting) {
					AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 等待游戏运行");
				} else { // entered func: threadLock()
					if (lockPid == 0) {
						AppendMenu(hMenu, MFT_STRING, IDM_TITLE, "SGuard限制器 - 正在分析");
					} else {
						char titleBuf[512] = "SGuard限制器 - ";
						switch (lockMode) {
							case 0:
								for (auto i = 0; i < 3; i++) {
									sprintf(titleBuf + strlen(titleBuf), "%x[%c] ", lockedThreads[i].tid, lockedThreads[i].locked ? 'O' : 'X');
								}
								break;
							case 1:
								for (auto i = 0; i < 3; i++) {
									sprintf(titleBuf + strlen(titleBuf), "%x[..] ", lockedThreads[i].tid);
								}
								break;
							case 2:
								sprintf(titleBuf + strlen(titleBuf), "%x[%c] ", lockedThreads[0].tid, lockedThreads[0].locked ? 'O' : 'X');
								break;
							case 3:
								sprintf(titleBuf + strlen(titleBuf), "%x[..] ", lockedThreads[0].tid);
								break;
						}
						AppendMenu(hMenu, MFT_STRING, IDM_TITLE, titleBuf);
					}
				}
				AppendMenu(hMenu, MFT_STRING, IDM_SWITCHMODE, "当前模式：线程追踪 [点击切换]");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3, "锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1, "弱锁定");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK3RR, "锁定-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_LOCK1RR, "弱锁定-rr");
				AppendMenu(hMenu, MFT_STRING, IDM_UNLOCK, "解除锁定");
				AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				if (lockMode == 1 || lockMode == 3) {
					char buf[128];
					sprintf(buf, "设置时间切分（当前：%d）", lockRound);
					AppendMenu(hMenu, MFT_STRING, IDM_SETRRTIME, buf);
					AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
				}
				AppendMenu(hMenu, MFT_STRING, IDM_EXIT, "退出");
				if (lockEnabled) {
					switch (lockMode) {
						case 0:
							CheckMenuItem(hMenu, IDM_LOCK3, MF_CHECKED);
							break;
						case 1:
							CheckMenuItem(hMenu, IDM_LOCK3RR, MF_CHECKED);
							break;
						case 2:
							CheckMenuItem(hMenu, IDM_LOCK1, MF_CHECKED);
							break;
						case 3:
							CheckMenuItem(hMenu, IDM_LOCK1RR, MF_CHECKED);
							break;
					}
				} else {
					CheckMenuItem(hMenu, IDM_UNLOCK, MF_CHECKED);
				}
			}
			GetCursorPos(&pt);
			SetForegroundWindow(g_hWnd);
			TrackPopupMenu(hMenu, TPM_LEFTALIGN, pt.x, pt.y, 0, g_hWnd, NULL);
			DestroyMenu(hMenu);
		} else if (lParam == WM_LBUTTONDBLCLK) {
			ShowAbout();
		}
	}
	break;
	case WM_COMMAND:
	{
		UINT id = LOWORD(wParam);
		switch (id) {
		case IDM_EXIT:
		{
			if (g_Mode == 0 && limitEnabled == true) {
				disableLimit();
			} else if (g_Mode == 1 && lockEnabled == true) {
				disableLock();
			}
			PostMessage(g_hWnd, WM_CLOSE, 0, 0);
		}
			break;
		case IDM_TITLE:
			ShowAbout();
			break;
		case IDM_SWITCHMODE:
			if (g_Mode == 0) {
				disableLimit();
				lockEnabled = true;
				g_Mode = 1;
			} else { // if g_Mode == 1
				disableLock();
				limitEnabled = true;
				g_Mode = 0;
			}
			writeConfig();
			break;

		case IDM_PERCENT90:
			limitEnabled = true;
			limitPercent = 90;
			writeConfig();
			break;
		case IDM_PERCENT95:
			limitEnabled = true;
			limitPercent = 95;
			writeConfig();
			break;
		case IDM_PERCENT99:
			limitEnabled = true;
			limitPercent = 99;
			writeConfig();
			break;
		case IDM_PERCENT999:
			limitEnabled = true;
			limitPercent = 999;
			writeConfig();
			break;
		case IDM_STOPLIMIT:
			disableLimit();
			break;

		case IDM_LOCK3:
			lockEnabled = true;
			lockMode = 0;
			writeConfig();
			break;
		case IDM_LOCK3RR:
			lockEnabled = true;
			lockMode = 1;
			writeConfig();
			break;
		case IDM_LOCK1:
			lockEnabled = true;
			lockMode = 2;
			writeConfig();
			break;
		case IDM_LOCK1RR:
			lockEnabled = true;
			lockMode = 3;
			writeConfig();
			break;
		case IDM_SETRRTIME:
			DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_SETTIMEDIALOG), g_hWnd, SetTimeDlgProc);
			writeConfig();
			break;
		case IDM_UNLOCK:
			disableLock();
			break;
		}
	}
	break;
	case WM_CLOSE:
	{
		DestroyWindow(g_hWnd);
	}
	break;
	case WM_DESTROY:
	{
		PostQuitMessage(0);
	}
	break;
	}

	return DefWindowProc(hWnd, msg, wParam, lParam);
}