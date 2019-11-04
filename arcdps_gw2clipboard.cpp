#define _CRT_SECURE_NO_WARNINGS

/*
* ArcDPS GW2Clipboard plugin
*
* This simple plugin opens gw2clipboard, or if already opened restores the window if applicable.
* When gw2 closes, this plugin withy notify gw2clipboard to minimize to the system tray.
*
* This plugin DOES NOT receive realtime api data
*
* Modified version of: https://www.deltaconnected.com/arcdps/api/arcdps_combatdemo.cpp
*/

#include <stdint.h>
#include <stdio.h>
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>
#include <string>
#include <exception>

namespace fs = std::filesystem;
using namespace std;

struct IDirect3DDevice9;

/* arcdps export table */
typedef struct arcdps_exports {
	uintptr_t size; /* size of exports table */
	uintptr_t sig; /* pick a number between 0 and uint64_t max that isn't used by other modules */
	char* out_name; /* name string */
	char* out_build; /* build string */
	void* wnd_nofilter; /* wndproc callback, fn(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) */
	void* combat; /* combat event callback, fn(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision) */
	void* imgui; /* id3dd9::present callback, before imgui::render, fn(uint32_t not_charsel_or_loading) */
	void* options_end; /* id3dd9::present callback, appending to the end of options window in arcdps, fn() */
	void* combat_local;  /* combat event callback like area but from chat log, fn(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision) */
	void* wnd_filter; /* wndproc callback like above, input filered using modifiers */
	void* options_windows; /* called once per 'window' option checkbox, with null at the end, non-zero return disables drawing that option, fn(char* windowname) */
} arcdps_exports;

/* combat event - see evtc docs for details, revision param in combat cb is equivalent of revision byte header */
typedef struct cbtevent {
	uint64_t time;
	uintptr_t src_agent;
	uintptr_t dst_agent;
	int32_t value;
	int32_t buff_dmg;
	uint32_t overstack_value;
	uint32_t skillid;
	uint16_t src_instid;
	uint16_t dst_instid;
	uint16_t src_master_instid;
	uint16_t dst_master_instid;
	uint8_t iff;
	uint8_t buff;
	uint8_t result;
	uint8_t is_activation;
	uint8_t is_buffremove;
	uint8_t is_ninety;
	uint8_t is_fifty;
	uint8_t is_moving;
	uint8_t is_statechange;
	uint8_t is_flanking;
	uint8_t is_shields;
	uint8_t is_offcycle;
	uint8_t pad61;
	uint8_t pad62;
	uint8_t pad63;
	uint8_t pad64;
} cbtevent;

/* agent short */
typedef struct ag {
	char* name; /* agent name. may be null. valid only at time of event. utf8 */
	uintptr_t id; /* agent unique identifier */
	uint32_t prof; /* profession at time of event. refer to evtc notes for identification */
	uint32_t elite; /* elite spec at time of event. refer to evtc notes for identification */
	uint32_t self; /* 1 if self, 0 if not */
	uint16_t team; /* sep21+ */
} ag;

/* proto/globals */
uint32_t cbtcount = 0;
arcdps_exports arc_exports;
char* arcvers;
string GW2ClipboardPath;
BOOL bExitOnClose = false;
void dll_init(HANDLE hModule);
void dll_exit();
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversionstr, void* imguicontext, IDirect3DDevice9 * id3dd9);
extern "C" __declspec(dllexport) void* get_release_addr();
arcdps_exports* mod_init();
uintptr_t mod_release();
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
uintptr_t mod_callback(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision);

/* dll main -- winapi */
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ulReasonForCall, LPVOID lpReserved) {
	switch (ulReasonForCall) {
	case DLL_PROCESS_ATTACH: dll_init(hModule); break;
	case DLL_PROCESS_DETACH: dll_exit(); break;

	case DLL_THREAD_ATTACH:  break;
	case DLL_THREAD_DETACH:  break;
	}
	return 1;
}

/* dll attach -- from winapi */
void dll_init(HANDLE hModule) {
	return;
}

/* dll detach -- from winapi */
void dll_exit() {
	return;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client load */
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversionstr, void* imguicontext, IDirect3DDevice9 * id3dd9) {
	arcvers = arcversionstr;
	//ImGui::SetCurrentContext((ImGuiContext*)imguicontext);
	return mod_init;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client exit */
extern "C" __declspec(dllexport) void* get_release_addr() {
	arcvers = 0;
	return mod_release;
}

BOOL gw2clipboard_ipc(LPCVOID command) {
	try {
		DWORD dwWritten;
		HANDLE hPipe = CreateFile(TEXT("\\\\.\\pipe\\pipe_gw2cp"),
			GENERIC_WRITE,
			0,
			NULL,
			OPEN_EXISTING,
			0,
			NULL);

		if (hPipe != INVALID_HANDLE_VALUE)
		{
			WriteFile(hPipe,
				command,
				(DWORD)strlen((char*)command) + 1,
				&dwWritten,
				NULL);

			CloseHandle(hPipe);
			return TRUE;
		}
	}
	catch (exception & e) {
		e;
	}
	return FALSE;
}

/* initialize mod -- return table that arcdps will use for callbacks */
arcdps_exports* mod_init() {
#ifdef _DEBUG
	AllocConsole();
#endif
	/* big buffer */
	char buff[4096];
	char* p = &buff[0];
	p += _snprintf(p, 400, "==== mod_init ====\n");
	p += _snprintf(p, 400, "arcdps: %s\n", arcvers);

	try {
		TCHAR gw2Filename[512];
		int bytes = GetModuleFileName(NULL, gw2Filename, 512);
		if (bytes > 0) {
			string s = gw2Filename;

			size_t i = s.rfind("\\", s.length());
			string path = s.substr(0, i + 1);
			string filename = s.substr(i + 1, s.length() - i);
			string addOnPath = path + "\\addons\\gw2clipboard";
			string settingFileName = addOnPath + "\\gw2clipboard.ini";

			fs::create_directories(addOnPath);
			if (fs::exists(settingFileName)) {
				ifstream file;
				file.open(settingFileName);
				for (string line; getline(file, line); )
				{
					size_t i = line.find("=");
					if (i != string::npos) {
						string option =  line.substr(0, i);
						string value = line.substr(i + 1, line.length() - i);
						if (option == "PATH") GW2ClipboardPath = value;
						if (option == "CLOSE" && value=="1") bExitOnClose = TRUE;
					}
				}
				file.close();
			}
			else {
				ofstream file(settingFileName);
				file << "PATH=" + addOnPath + "\n";
				file << "CLOSE=1\n";
				file.close();

				GW2ClipboardPath = addOnPath;
				bExitOnClose = TRUE;
			}
			if (!GW2ClipboardPath.empty() && !fs::exists(GW2ClipboardPath + "\\GW2Clipboard.exe")) GW2ClipboardPath.clear();

			if (i != string::npos) {
				p += _snprintf(p, 400, "path: %s\n", path.c_str());
				p += _snprintf(p, 400, "filename: %s\n", filename.c_str());
				p += _snprintf(p, 400, "gw2clipboard exe: %s\n", (GW2ClipboardPath + "\\GW2Clipboard.exe").c_str());
			}
		}
		throw exception("why not");
	}
	catch (exception & e) {
		p += _snprintf(p, 400, "Exception: %s\n", e.what());
	}

	if (!GW2ClipboardPath.empty()) {
		if (!gw2clipboard_ipc("mod_init\n")) {
			p += _snprintf(p, 400, "gw2clipboard IPC not found\n");
			ShellExecute(NULL, "open", "gw2clipboard.exe", NULL, GW2ClipboardPath.c_str(), SW_SHOWDEFAULT);
		}
	}
	else {
		p += _snprintf(p, 400, "Could not resolve gw2clipboard.exe at: %s\n", GW2ClipboardPath.c_str());
	}

#ifdef _DEBUG
	/* print */
	DWORD written = 0;
	HANDLE hnd = GetStdHandle(STD_OUTPUT_HANDLE);
	WriteConsoleA(hnd, &buff[0], (DWORD)(p - &buff[0]), &written, 0);
#endif

	char name[] = "gw2clipboard";
	char version[] = "1.0";

	/* for arcdps */
	memset(&arc_exports, 0, sizeof(arcdps_exports));
	arc_exports.sig = 0x7331;
	arc_exports.size = sizeof(arcdps_exports);
	arc_exports.out_name = name;
	arc_exports.out_build = version;

#ifdef _DEBUG
	arc_exports.wnd_nofilter = mod_wnd;
	arc_exports.combat = mod_callback;
#else
	arc_exports.wnd_nofilter = NULL;
	arc_exports.combat = NULL;
#endif // _DEBUG
	//arc_exports.size = (uintptr_t)"error message if you decide to not load, sig must be 0";

	return &arc_exports;
}

/* release mod -- return ignored */
uintptr_t mod_release() {
	if (!GW2ClipboardPath.empty()) {
		if (bExitOnClose) gw2clipboard_ipc("mod_release:exit\n");
		else gw2clipboard_ipc("mod_release\n");
	}

#ifdef _DEBUG
	FreeConsole();
#endif // _DEBUG

	return 0;
}

/* window callback -- return is assigned to umsg (return zero to not be processed by arcdps or game) */
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	return uMsg;
}

/* mod callback -- may be called asynchronously. return ignored */
uintptr_t mod_callback(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision) {
	return 0;
}