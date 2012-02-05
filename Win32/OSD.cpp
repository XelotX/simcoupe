// Part of SimCoupe - A SAM Coupe emulator
//
// OSD.cpp: Win32 common OS-dependant functions
//
//  Copyright (c) 1999-2012 Simon Owen
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "SimCoupe.h"

#include "OSD.h"
#include "CPU.h"
#include "Frame.h"
#include "Main.h"
#include "Options.h"
#include "Parallel.h"
#include "UI.h"
#include "Video.h"

HANDLE g_hEvent;
MMRESULT g_hTimer;

HINSTANCE g_hinstDDraw, g_hinstDInput, g_hinstDSound;

PFNDIRECTDRAWCREATE pfnDirectDrawCreate;
PFNDIRECTINPUTCREATE pfnDirectInputCreate;
PFNDIRECTSOUNDCREATE pfnDirectSoundCreate;


// Timer handler, called every 20ms - seemed more reliable than having it set the event directly, for some weird reason
void CALLBACK TimeCallback (UINT uTimerID_, UINT uMsg_, DWORD_PTR dwUser_, DWORD_PTR dw1_, DWORD_PTR dw2_)
{
    // Signal that the next frame is due
    SetEvent(g_hEvent);
}


bool OSD::Init (bool fFirstInit_/*=false*/)
{
    UI::Exit(true);
    TRACE("-> OSD::Init(%s)\n", fFirstInit_ ? "first" : "");

    bool fRet = false;

    if (fFirstInit_)
    {
        g_hinstDDraw  = LoadLibrary("DDRAW.DLL");
        g_hinstDInput = LoadLibrary("DINPUT.DLL");
        g_hinstDSound = LoadLibrary("DSOUND.DLL");

        if (g_hinstDDraw) pfnDirectDrawCreate = reinterpret_cast<PFNDIRECTDRAWCREATE>(GetProcAddress(g_hinstDDraw, "DirectDrawCreate"));
        if (g_hinstDInput) pfnDirectInputCreate = reinterpret_cast<PFNDIRECTINPUTCREATE>(GetProcAddress(g_hinstDInput, "DirectInputCreateA"));
        if (g_hinstDSound) pfnDirectSoundCreate = reinterpret_cast<PFNDIRECTSOUNDCREATE>(GetProcAddress(g_hinstDSound, "DirectSoundCreate"));

        if (!pfnDirectDrawCreate || !pfnDirectInputCreate || !pfnDirectSoundCreate)
        {
            Message(msgError, "This program requires DirectX 3 or later to be installed.");
            return false;
        }

        // Initialise Windows common controls
        InitCommonControls();

        // We'll do our own error handling, so suppress any windows error dialogs
        SetErrorMode(SEM_FAILCRITICALERRORS);

        // Create an event that will be set every 20ms for the 50Hz sync
        if (!(g_hEvent = CreateEvent(NULL, FALSE, FALSE, NULL)))
            Message(msgWarning, "Failed to create sync event object (%#08lx)", GetLastError());

        // Set a timer to fire every every 20ms for our 50Hz frame synchronisation
        else if (!(g_hTimer = timeSetEvent(1000/EMULATED_FRAMES_PER_SECOND, 0, TimeCallback, 0, TIME_PERIODIC|TIME_CALLBACK_FUNCTION)))
            Message(msgWarning, "Failed to start sync timer (%#08lx)", GetLastError());
    }

    fRet = UI::Init(fFirstInit_);

    TRACE("<- OSD::Init() returning %s\n", fRet ? "true" : "false");
    return fRet;
}

void OSD::Exit (bool fReInit_/*=false*/)
{
    if (!fReInit_)
    {
        if (g_hEvent)   { CloseHandle(g_hEvent); g_hEvent = NULL; }
        if (g_hTimer)   { timeKillEvent(g_hTimer); g_hTimer = NULL; }

        if (g_hinstDDraw)  { FreeLibrary(g_hinstDDraw);  g_hinstDDraw  = NULL; pfnDirectDrawCreate=NULL;  }
        if (g_hinstDInput) { FreeLibrary(g_hinstDInput); g_hinstDInput = NULL; pfnDirectInputCreate=NULL; }
        if (g_hinstDSound) { FreeLibrary(g_hinstDSound); g_hinstDSound = NULL; pfnDirectSoundCreate=NULL; }
    }

    UI::Exit(fReInit_);
}


// Return a time-stamp in milliseconds
DWORD OSD::GetTime ()
{
    static LARGE_INTEGER llFreq;
    LARGE_INTEGER llNow;

    // Read high frequency counter, falling back on the multimedia timer
    if (!llFreq.QuadPart && !QueryPerformanceFrequency(&llFreq))
        return timeGetTime();

    // Read the current 64-bit time value, falling back on the multimedia timer
    QueryPerformanceCounter(&llNow);
    return static_cast<DWORD>((llNow.QuadPart * 1000i64) / llFreq.QuadPart);
}


// Do whatever is necessary to locate an additional SimCoupe file - The Win32 version looks in the
// same directory as the EXE, but other platforms could use an environment variable, etc.
// If the path is already fully qualified (an OS-specific decision), use the original string
const char* OSD::GetFilePath (const char* pcszFile_/*=""*/)
{
    static char szPath[MAX_PATH];

    // If the supplied path looks absolute, use it as-is
    if (*pcszFile_ == '\\' || strchr(pcszFile_, ':'))
        lstrcpyn(szPath, pcszFile_, sizeof(szPath));

    // Form the full path relative to the current EXE file
    else
    {
        // Get the full path of the running module
        GetModuleFileName(__hinstance, szPath, sizeof szPath);

        // Strip the module file and append the supplied file/path
        lstrcpy(strrchr(szPath, '\\')+1, pcszFile_);
    }

    // Return a pointer to the new path
    return szPath;
}

// Same as GetFilePath but ensures a trailing backslash
const char* OSD::GetDirPath (const char* pcszDir_/*=""*/)
{
    char* psz = const_cast<char*>(GetFilePath(pcszDir_));

    // Append a backslash to non-empty strings that don't already have one
    if (*psz && psz[lstrlen(psz)-1] != '\\')
        strcat(psz, "\\");

    return psz;
}


// Check whether the specified path is accessible
bool OSD::CheckPathAccess (const char* pcszPath_)
{
    return !access(pcszPath_, X_OK);
}


// Return whether a file/directory is normally hidden from a directory listing
bool OSD::IsHidden (const char* pcszFile_)
{
    // Hide entries with the hidden or system attribute bits set
    DWORD dwAttrs = GetFileAttributes(pcszFile_);
    return (dwAttrs != 0xffffffff) && (dwAttrs & (FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_SYSTEM));
}

// Return the path to use for a given drive with direct floppy access
const char* OSD::GetFloppyDevice (int nDrive_)
{
    static char szDevice[] = "_:";

    szDevice[0] = 'A' + nDrive_-1;
    return szDevice;
}


void OSD::DebugTrace (const char* pcsz_)
{
    OutputDebugString(pcsz_);
}

void OSD::FrameSync ()
{
    switch (GetOption(sync))
    {
        case 1:
            ResetEvent(g_hEvent);
            WaitForSingleObject(g_hEvent, INFINITE);
            break;

        case 3:
            pdd->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL);
            // Fall through...

        case 2:
            pdd->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL);
            break;
    }
}

////////////////////////////////////////////////////////////////////////////////

CPrinterDevice::CPrinterDevice ()
    : m_hPrinter(INVALID_HANDLE_VALUE)
{
}

CPrinterDevice::~CPrinterDevice ()
{
    Close();
}


bool CPrinterDevice::Open ()
{
    PRINTER_DEFAULTS pd = { "RAW", NULL, PRINTER_ACCESS_USE };

    if (m_hPrinter != INVALID_HANDLE_VALUE)
        return true;

    if (OpenPrinter(const_cast<char*>(GetOption(printerdev)), &m_hPrinter, &pd))
    {
        DOC_INFO_1 docinfo;
        docinfo.pDocName = "SimCoupe print";
        docinfo.pOutputFile = NULL;
        docinfo.pDatatype = "RAW";

        // Start the job
        if (StartDocPrinter(m_hPrinter, 1, reinterpret_cast<BYTE*>(&docinfo)) && StartPagePrinter(m_hPrinter))
            return true;

        ClosePrinter(m_hPrinter);
    }

    Frame::SetStatus("Failed to open %s", GetOption(printerdev));
    return false;
}

void CPrinterDevice::Close ()
{
    if (m_hPrinter != INVALID_HANDLE_VALUE)
    {
        EndPagePrinter(m_hPrinter);
        EndDocPrinter(m_hPrinter);

        ClosePrinter(m_hPrinter);
        m_hPrinter = INVALID_HANDLE_VALUE;

        Frame::SetStatus("Printed to %s", GetOption(printerdev));
    }
}

void CPrinterDevice::Write (BYTE *pb_, size_t uLen_)
{
    if (m_hPrinter != INVALID_HANDLE_VALUE)
    {
        DWORD dwWritten;

        if (!WritePrinter(m_hPrinter, pb_, static_cast<DWORD>(uLen_), &dwWritten))
        {
            Close();
            Frame::SetStatus("Printer error!");
        }
    }
}


// Win32 lacks a few of the required POSIX functions, so we'll implement them ourselves...

WIN32_FIND_DATA s_fd;
struct dirent s_dir;

DIR* opendir (const char* pcszDir_)
{
    static char szPath[MAX_PATH];

    memset(&s_dir, 0, sizeof s_dir);

    // Append a wildcard to match all files
    lstrcpy(szPath, pcszDir_);
    if (szPath[lstrlen(szPath)-1] != '\\')
        lstrcat(szPath, "\\");
    lstrcat(szPath, "*");

    // Find the first file, saving the details for later
    HANDLE h = FindFirstFile(szPath, &s_fd);

    // Return the handle if successful, otherwise NULL
    return (h == INVALID_HANDLE_VALUE) ? NULL : reinterpret_cast<DIR*>(h);
}

struct dirent* readdir (DIR* hDir_)
{
    // All done?
    if (!s_fd.cFileName[0])
        return NULL;

    // Copy the filename and set the length
    s_dir.d_reclen = lstrlen(lstrcpyn(s_dir.d_name, s_fd.cFileName, sizeof s_dir.d_name));

    // If we'd already reached the end
    if (!FindNextFile(reinterpret_cast<HANDLE>(hDir_), &s_fd))
        s_fd.cFileName[0] = '\0';

    // Return the current entry
    return &s_dir;
}

int closedir (DIR* hDir_)
{
    return FindClose(reinterpret_cast<HANDLE>(hDir_)) ? 0 : -1;
}
