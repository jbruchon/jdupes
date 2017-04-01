/*
 * Windows-native code for getting stat()-like information
 *
 * Copyright (C) 2017 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License
 */

#include "win_stat.h"
#include <stdint.h>
#ifndef WIN32_LEAN_AND_MEAN
 #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

/* Convert NT epoch to UNIX epoch */
static time_t nttime_to_unixtime(const uint64_t * const restrict timestamp)
{
	uint64_t newstamp;

	memcpy(&newstamp, timestamp, sizeof(uint64_t));
	newstamp /= 10000000LL;
	if (newstamp <= 11644473600LL) return 0;
	newstamp -= 11644473600LL;
	return newstamp;
}

/* Get stat()-like extra information for a file on Windows */
int win_stat(const char * const filename, struct winstat * const restrict buf)
{
  HANDLE hFile;
  BY_HANDLE_FILE_INFORMATION bhfi;
  uint64_t timetemp;

#ifdef UNICODE
  static wchar_t wname[PATH_MAX];
  if (!buf) return -127;
  if (!MultiByteToWideChar(CP_UTF8, 0, filename, -1, wname, PATH_MAX)) return -126;
  hFile = CreateFileW(wname, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		  FILE_FLAG_BACKUP_SEMANTICS, NULL);
#else
  if (!buf) return -127;
  hFile = CreateFile(filename, 0, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		  FILE_FLAG_BACKUP_SEMANTICS, NULL);
#endif

  if (hFile == INVALID_HANDLE_VALUE) goto failure;
  if (!GetFileInformationByHandle(hFile, &bhfi)) goto failure2;

  buf->file = ((uint64_t)(bhfi.nFileIndexHigh) << 32) + (uint64_t)bhfi.nFileIndexLow;
  buf->size = ((uint64_t)(bhfi.nFileSizeHigh) << 32) + (uint64_t)bhfi.nFileSizeLow;
  timetemp = ((uint64_t)(bhfi.ftCreationTime.dwHighDateTime) << 32) + bhfi.ftCreationTime.dwLowDateTime;
  buf->ctime = nttime_to_unixtime(&timetemp);
  timetemp = ((uint64_t)(bhfi.ftLastWriteTime.dwHighDateTime) << 32) + bhfi.ftLastWriteTime.dwLowDateTime;
  buf->mtime = nttime_to_unixtime(&timetemp);
  timetemp = ((uint64_t)(bhfi.ftLastAccessTime.dwHighDateTime) << 32) + bhfi.ftLastAccessTime.dwLowDateTime;
  buf->atime = nttime_to_unixtime(&timetemp);
  buf->device = (uint32_t)bhfi.dwVolumeSerialNumber;
  buf->nlink = (uint32_t)bhfi.nNumberOfLinks;
  buf->mode = (uint32_t)bhfi.dwFileAttributes;

  CloseHandle(hFile);
  return 0;

failure:
  CloseHandle(hFile);
  return -1;
failure2:
  CloseHandle(hFile);
  return -2;
}
