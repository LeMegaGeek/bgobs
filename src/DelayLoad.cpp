// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <windows.h>
#include <delayimp.h>

#include <filesystem>
#include <string>

#include <obs-module.h>

extern "C" {

extern IMAGE_DOS_HEADER __ImageBase;

FARPROC WINAPI DelayLoadHook(unsigned dliNotify, PDelayLoadInfo pdli)
{
	if (dliNotify == dliNotePreLoadLibrary) {
		const std::string dllName(pdli->szDll);
		if (dllName == "onnxruntime.dll") {
			wchar_t modulePath[MAX_PATH] = {};
			GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
			const std::filesystem::path absPath =
				std::filesystem::path(modulePath).parent_path() / L"onnxruntime.dll";
			obs_log(LOG_INFO, "Loading onnxruntime.dll from plugin binary directory");
			return (FARPROC)LoadLibraryExW(absPath.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
		} else {
			return NULL;
		}
	}
	return NULL;
}

const PfnDliHook __pfnDliNotifyHook2 = DelayLoadHook;
}
