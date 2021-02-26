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
#include "imgui/imgui.h"

namespace fs = std::filesystem;
using namespace std;

struct IDirect3DDevice9;

struct ImGuiContext;

/* arcdps export table */
typedef struct arcdps_exports {
	uintptr_t size; /* size of exports table */
	uint32_t sig; /* pick a number between 0 and uint32_t max that isn't used by other modules */
	uint32_t imguivers; /* set this to IMGUI_VERSION_NUM. if you don't use imgui, 18000 (as of 2021-02-02) */
	const char* out_name; /* name string */
	const char* out_build; /* build string */
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

struct config_settings {
	string gw2Path;
	string addOnPath;
	string gw2ClipboardExePath;
	string gw2ExeFileName;
	string settingFileName;

	HANDLE hStdOut = NULL;
	BOOL bExitOnClose = FALSE;
	BOOL Log = FALSE;
};

config_settings config;

/* proto/globals */
uint32_t cbtcount = 0;
arcdps_exports arc_exports;
char* arcvers;

void dll_init(HANDLE hModule);
void dll_exit();
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversion, ImGuiContext * imguictx, void* id3dd9, HANDLE arcdll, void* mallocfn, void* freefn);
extern "C" __declspec(dllexport) void* get_release_addr();
arcdps_exports* mod_init();
uintptr_t mod_release();
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
uintptr_t mod_callback(cbtevent* ev, ag* src, ag* dst, char* skillname, uint64_t id, uint64_t revision);

BOOL configure();
void debug_log(string msg, BOOL append);
void debug_console(const char* msg);
BOOL gw2clipboard_ipc(LPCVOID command);

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


void debug_console(const char* msg) {
#ifdef _DEBUG
	DWORD written;
	WriteConsole(config.hStdOut, msg, (DWORD)strlen(msg), &written, NULL);
#endif
}

void debug_log(string msg, BOOL append = TRUE) {
	if (config.Log) {
		ofstream outfile;

		outfile.open(config.addOnPath + "\\arcdps_gw2clipboard_debug.txt", append ? ios_base::app : ios_base::trunc);
		outfile << msg << "\n";

		outfile.close();
	}
	debug_console((msg + "\n").c_str());
}

BOOL gw2clipboard_ipc(LPCVOID command) {
	debug_log("gw2clipboard_ipc");
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

			FlushFileBuffers(hPipe);
			CloseHandle(hPipe);
			debug_log("gw2clipboard_ipc - sent");
			return TRUE;
		}
	}
	catch (exception & e) {
		debug_log("gw2clipboard_ipc - failed: " + string(e.what()));
		e;
	}
	return FALSE;
}

/* dll attach -- from winapi */
void dll_init(HANDLE hModule) {
#ifdef _DEBUG
	AllocConsole();
	config.hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
#endif
	configure();
	debug_log("dll_init");
	return;
}

/* dll detach -- from winapi */
void dll_exit() {
	debug_log("dll_exit");
	if (!config.gw2ClipboardExePath.empty()) {
		if (config.bExitOnClose) gw2clipboard_ipc("mod_release:exit\n");
		else gw2clipboard_ipc("mod_release\n");
	}

#ifdef _DEBUG
	FreeConsole();
#endif // _DEBUG
	return;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client load */
extern "C" __declspec(dllexport) void* get_init_addr(char* arcversion, ImGuiContext * imguictx, void* id3dd9, HANDLE arcdll, void* mallocfn, void* freefn) {
	debug_log("get_init_addr");
	arcvers = arcversion;
	//filelog = (void*)GetProcAddress((HMODULE)arcdll, "e3");
	//arclog = (void*)GetProcAddress((HMODULE)arcdll, "e8");
	ImGui::SetCurrentContext((ImGuiContext*)imguictx);
	ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))mallocfn, (void (*)(void*, void*))freefn); // on imgui 1.80+

	return mod_init;
}

/* export -- arcdps looks for this exported function and calls the address it returns on client exit */
extern "C" __declspec(dllexport) void* get_release_addr() {
	debug_log("get_release_addr");
	arcvers = 0;
	return mod_release;
}

BOOL configure() {
	debug_log("configure", FALSE);
	try {
		TCHAR gw2Filename[512];
		int bytes = GetModuleFileName(NULL, gw2Filename, 512);
		if (bytes > 0) {
			string s = gw2Filename;

			size_t i = s.rfind("\\", s.length());
			config.gw2Path = s.substr(0, i);
			config.gw2ExeFileName = s.substr(i + 1, s.length() - i);
			config.addOnPath = config.gw2Path + "\\addons\\gw2clipboard";
			config.settingFileName = config.addOnPath + "\\gw2clipboard.ini";

			fs::create_directories(config.addOnPath);
			if (fs::exists(config.settingFileName)) {
				ifstream file;
				file.open(config.settingFileName);
				for (string line; getline(file, line); )
				{
					size_t i = line.find("=");
					if (i != string::npos) {
						string option = line.substr(0, i);
						string value = line.substr(i + 1, line.length() - i);
						if (option == "PATH") config.gw2ClipboardExePath = value;
						if (option == "CLOSE" && value == "1") config.bExitOnClose = TRUE;
						if (option == "LOG" && value == "1") config.Log = TRUE;
					}
				}
				file.close();
			}
			else {
				ofstream file(config.settingFileName);
				file << "PATH=" + config.addOnPath + "\n";
				file << "CLOSE=1\n";
				file << "LOG=0\n";
				file.close();

				config.gw2ClipboardExePath = config.addOnPath;
				config.bExitOnClose = TRUE;
				config.Log = FALSE;
			}
			if (!config.gw2ClipboardExePath.empty() && !fs::exists(config.gw2ClipboardExePath + "\\GW2Clipboard.exe")) config.gw2ClipboardExePath.clear();

			debug_log("Config:\ngw2Path:" + config.gw2Path + "\naddOnPath:" + config.addOnPath + "\nsettingFileName:" + config.settingFileName + "\ngw2ClipboardPath:" + config.gw2ClipboardExePath);

			return TRUE;
		}
	}
	catch (exception & e) {
		debug_log("Exception" + string(e.what()));
	}

	return FALSE;
}



uintptr_t mod_imgui(uint32_t not_charsel_or_loading) {
	return 0;
}

/* initialize mod -- return table that arcdps will use for callbacks */
arcdps_exports* mod_init() {
	debug_log("==== mod_init ====");
	debug_log("arcdps: " + string(arcvers));

	if (!config.gw2ClipboardExePath.empty()) {
		if (!gw2clipboard_ipc("mod_init\n")) {
			debug_log("gw2clipboard IPC not found");
			ShellExecute(NULL, "open", "gw2clipboard.exe", NULL, config.gw2ClipboardExePath.c_str(), SW_SHOWDEFAULT);
		}
	}
	else {
		debug_log("Could not resolve gw2clipboard.exe at: " + config.gw2ClipboardExePath);
	}

	static char name[] = "gw2clipboard";
	static char version[] = "1.0";

	/* for arcdps */
	memset(&arc_exports, 0, sizeof(arcdps_exports));
	arc_exports.sig = 0x7331;
	arc_exports.size = sizeof(arcdps_exports);
	arc_exports.imguivers = IMGUI_VERSION_NUM;
	arc_exports.out_name = name;
	arc_exports.out_build = version;

#ifdef _DEBUG
	arc_exports.wnd_nofilter = mod_wnd;
	arc_exports.combat = mod_callback;
#else
	arc_exports.wnd_nofilter = NULL;
	arc_exports.combat = NULL;
	arc_exports.options_end = NULL;
#endif
	arc_exports.imgui = mod_imgui;
	// _DEBUG
	//arc_exports.size = (uintptr_t)"error message if you decide to not load, sig must be 0";

	return &arc_exports;
}

/* release mod -- return ignored */
uintptr_t mod_release() {
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