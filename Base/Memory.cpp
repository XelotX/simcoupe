// Part of SimCoupe - A SAM Coup� emulator
//
// Memory.cpp: Memory configuration and management
//
//  Copyright (c) 1996-2001  Allan Skillman
//  Copyright (c) 1999-2001  Simon Owen
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

// Changes 1999-2001 by Simon Owen
//  - added support for real 256K memory configuration
//  - added fast boot option by temporarily patching the SAM ROMs
//  - display memory writes now now catch-up/update the frame image
//  - mode 1 look-up tables built during initialisation

// ToDo:
//  - finish the ATOM auto-boot code that was started

#include "SimCoupe.h"
#include "Memory.h"

#include "CPU.h"
#include "Options.h"
#include "OSD.h"
#include "CStream.h"
#include "Util.h"

////////////////////////////////////////////////////////////////////////////////

// Single block holding all memory needed
BYTE* pMemory;

// Master read and write lists that are static for a given memory configuration
BYTE* apbPageReadPtrs[TOTAL_PAGES];
BYTE* apbPageWritePtrs[TOTAL_PAGES];

// Page numbers present in each of the 4 sections in the 64K address range
int anSectionPages[4];
bool afContendedPages[4];

// Array of pointers for memory to use when reading from or writing to each each section
BYTE* apbSectionReadPtrs[4];
BYTE* apbSectionWritePtrs[4];

// Look-up tables for fast mapping between mode 1 display addresses and line numbers
WORD g_awMode1LineToByte[SCREEN_LINES];
BYTE g_abMode1ByteToLine[SCREEN_LINES*SCREEN_BLOCKS];

////////////////////////////////////////////////////////////////////////////////

static int LoadRomImage (const char* pcszImage_, int nPage_);


// Allocate and initialise memory
bool Memory::Init (bool fFirstInit_/*=false*/)
{
    // Memory only requires one-off initialisation
    if (fFirstInit_)
    {
        // Allocate a single block for all we're likely to need (512K + 4MB external + 2 ROMs + 2 scratch)
        if (!pMemory && !(pMemory = new BYTE[TOTAL_PAGES * MEM_PAGE_SIZE]))
        {
            Message(msgError, "Out of memory!");
            return false;
        }

        // Stripe-initialise the RAM banks as seems to be the situation on the real unit
        for (int i = 0 ; i < (ROM0 * MEM_PAGE_SIZE) ; i += 256)
        {
            memset(pMemory+i,     0x00, 128);
            memset(pMemory+i+128, 0xff, 128);
        }

        // Fill the ROMs and scratch memory with 0xff for now
        memset(pMemory+ROM0 * MEM_PAGE_SIZE, 0xff, (TOTAL_PAGES-ROM0) * MEM_PAGE_SIZE);
    }


    // Start with all memory available, and fully readable and writable
    for (int nPage = 0 ; nPage < TOTAL_PAGES ; nPage++)
        apbPageReadPtrs[nPage] = apbPageWritePtrs[nPage] = pMemory + (nPage * MEM_PAGE_SIZE);

    // Are we in 256K mode?
    if (GetOption(mainmem) == 256)
    {
        // Invalidate the top 256K of main memory
        for (int i = N_PAGES_MAIN/2 ; i < N_PAGES_MAIN ; i++)
            apbPageReadPtrs[i] = (apbPageWritePtrs[i] = apbPageWritePtrs[SCRATCH_WRITE]) - MEM_PAGE_SIZE;
    }

    // Make any absent portions of external memory read-only (note: external memory is added top-down!)
    int nExternal = min(GetOption(externalmem), MAX_EXTERNAL_MB);
    for (int nExt = 0 ; nExt < (MAX_EXTERNAL_MB - nExternal) * N_PAGES_1MB ; nExt++)
        apbPageWritePtrs[N_PAGES_MAIN + nExt] = apbPageWritePtrs[SCRATCH_WRITE];


    // Build the tables for fast mapping between mode 1 display addresses and line numbers
    for (int nOffset = 0 ; nOffset < (int)sizeof g_abMode1ByteToLine ; nOffset++)
    {
        g_abMode1ByteToLine[nOffset] = ((nOffset >> 5) & 0xc0) + ((nOffset >> 2) & 0x38) + ((nOffset >> 8) & 0x07);
        g_awMode1LineToByte[g_abMode1ByteToLine[nOffset]] = (nOffset & ~0x1f);
    }

    // Load the ROMs, using the second file if the first doesn't contain both
    if (LoadRomImage(GetOption(rom0), ROM0) < 2)
        LoadRomImage(GetOption(rom1), ROM1);

    // Writes to the ROM are discarded
    apbPageWritePtrs[ROM0] = apbPageWritePtrs[ROM1] = apbPageWritePtrs[SCRATCH_WRITE];

    return true;
}

void Memory::Exit (bool fReInit_/*=false*/)
{
    if (!fReInit_) { delete pMemory; pMemory = NULL; }
}


// Read the ROM image into the ROM area of our paged memory block
static int LoadRomImage (const char* pcszImage_, int nPage_)
{
    int nRead = 0;
    CStream* pROM;

    // Attempt to open the ROM file from the SimCoup� directory, and then from anywhere in it can be found
    if (!pcszImage_ || (!(pROM = CStream::Open(OSD::GetFilePath(pcszImage_))) && !(pROM = CStream::Open(pcszImage_))))
        Message(msgError, "Failed to open ROM image: %s", pcszImage_);
    else
    {
        // Read the header+bootstrap code from what could be a ZX82 file (for Andy Wright's ROM images)
        BYTE abHeader[140];
        pROM->Read(abHeader, sizeof abHeader);

        // If we don't find the ZX82 signature, rewind to read as a plain ROM file
        if (memcmp(abHeader, "ZX82", 4))
            pROM->Rewind();

        // Read the ROM(s), attempting to get both from the file if possible, then close the file
        nRead = pROM->Read(apbPageReadPtrs[nPage_], MEM_PAGE_SIZE * ((nPage_ == ROM0) ? 2 : 1));
        delete pROM;
    }

    // Return the number of ROMs read
    return nRead / MEM_PAGE_SIZE;
}
