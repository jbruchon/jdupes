/* jdupes progress indicator
   see jdupes.c for licensing information */

#include <stdio.h>
#include <inttypes.h>
#include "jdupes.h"
#include "likely_unlikely.h"

/** Windows 7+ taskbar progress functions **/
#ifdef ON_WINDOWS
static int com_initialized = 0;

void init_taskbar_progress(void) {
  if (SUCCEEDED(CoInitialize(NULL))) com_initialized = 1;
  else return;

  /* This will fail on XP/Vista and return a non-S_OK HRESULT */
  HRESULT hr = CoCreateInstance(
    &CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER,
    &IID_ITaskbarList3, (void **)&pTaskbarList
  );

  if (SUCCEEDED(hr) && pTaskbarList) {
    hwnd = GetConsoleWindow();
    /* verify vtable exists before calling */
    if (!hwnd || !pTaskbarList->lpVtbl) {
      pTaskbarList->lpVtbl->Release(pTaskbarList);
      pTaskbarList = NULL;
    }
  } else {
    pTaskbarList = NULL; /* Do nothing if Windows is too old */
  }
  return;
}


void update_taskbar_progress(unsigned long long current, unsigned long long total) {
  /* If pTaskbarList == NULL (XP/Vista), don't do anything */
  if (pTaskbarList && hwnd) {
    pTaskbarList->lpVtbl->SetProgressState(pTaskbarList, hwnd, TBPF_NORMAL);
    pTaskbarList->lpVtbl->SetProgressValue(pTaskbarList, hwnd, current, total);
  }
  return;
}


void cleanup_taskbar_progress(void) {
  if (pTaskbarList) {
    pTaskbarList->lpVtbl->SetProgressState(pTaskbarList, hwnd, TBPF_NOPROGRESS);
    pTaskbarList->lpVtbl->Release(pTaskbarList);
    pTaskbarList = NULL;
  }
  if (com_initialized == 1) {
    CoUninitialize();
    com_initialized = 0;
  }
  return;
}


/* Puts taskbar progress into numberless "spinning" mode */
void set_taskbar_spinning_progress(void) {
  if (pTaskbarList && hwnd) pTaskbarList->lpVtbl->SetProgressState(pTaskbarList, hwnd, TBPF_INDETERMINATE);
  return;
}
#endif /* ON_WINDOWS */


void update_phase1_progress(const char * const restrict type)
{
  fprintf(stderr, "\rScanning: %" PRIuMAX " files, %" PRIuMAX " %s (in %u specified)",
          progress, item_progress, type, user_item_count);
//  fflush(stderr);
}


/* Update progress indicator if requested */
void update_phase2_progress(const char * const restrict msg, const int file_percent)
{
  static int did_fpct = 0;
  static uintmax_t percent;

  percent = (progress * 100) / filecount;
  fprintf(stderr, "\rProgress [%" PRIuMAX "/%" PRIuMAX ", %" PRIuMAX " pairs matched] %" PRIuMAX "%%",
    progress, filecount, dupecount, percent);
  if (file_percent > -1 && msg != NULL) {
    fprintf(stderr, "  (%s: %d%%)         ", msg, file_percent);
    did_fpct = 1;
  } else if (did_fpct != 0) {
    fprintf(stderr, "                     ");
    did_fpct = 0;
  }
#ifdef ON_WINDOWS
  update_taskbar_progress(progress, filecount);
#endif
//  fflush(stderr);
  return;
}
