#pragma once

#include <Windows.h>

struct lockedThreads_t {
	DWORD  tid    = 0;
	HANDLE handle = NULL;   // handle == NULL : not locked.
	bool   locked = false;
};


void threadChase(DWORD);