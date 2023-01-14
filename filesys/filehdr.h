// filehdr.h
//	Data structures for managing a disk file header.
//
//	A file header describes where on disk to find the data in a file,
//	along with other information about the file (for instance, its
//	length, owner, etc.)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#ifndef FILEHDR_H
#define FILEHDR_H

#include "disk.h"
#include "pbitmap.h"

// MP4 start
const int NumSectorInt = (SectorSize / sizeof(int));
const int NumPointers = ((SectorSize - 2 * sizeof(int)) / sizeof(int));
const int MaxDirectBytes = (NumPointers * SectorSize);
const int MaxSingleIndirectBytes = (NumPointers * NumSectorInt * SectorSize);
const int MaxDoubleIndirectBytes = (NumPointers * NumSectorInt * NumSectorInt * SectorSize);
const int MaxTripleIndirectBytes = (NumPointers * NumSectorInt * NumSectorInt * NumSectorInt * SectorSize);
const int MaxFileSize = (MaxTripleIndirectBytes);
const int FileHeaderDiskSize = (sizeof(int) + sizeof(int) + NumPointers * SectorSize);
const int sizePerPointer[4] = {SectorSize, NumSectorInt *SectorSize, NumSectorInt *NumSectorInt *SectorSize, NumSectorInt *NumSectorInt *NumSectorInt *SectorSize};
// Mp4 end

// MP4 Start
class IndexBlock {
   public:
    IndexBlock(int level);
    ~IndexBlock();
    bool Allocate(PersistentBitmap *freeMap, int remSize);
    void Deallocate(PersistentBitmap *freeMap);
    void FetchFrom(int sector, int remSize);
    void WriteBack(int sector);
    int ByteToSector(int offset);
    void PrintSectors();
    void PrintContents();

   private:
    int level;
    int numBytes;
    int numSectors;
    int levelSectors;
    int nextSectors[NumSectorInt];
    IndexBlock **nextIndexBlocks;
};
// MP4 end

// The following class defines the Nachos "file header" (in UNIX terms,
// the "i-node"), describing where on disk to find all of the data in the file.
// The file header is organized as a simple table of pointers to
// data blocks.
//
// The file header data structure can be stored in memory or on disk.
// When it is on disk, it is stored in a single sector -- this means
// that we assume the size of this data structure to be the same
// as one disk sector.  Without indirect addressing, this
// limits the maximum file length to just under 4K bytes.
//
// There is no constructor; rather the file header can be initialized
// by allocating blocks for the file (if it is a new file), or by
// reading it from disk.

class FileHeader {
   public:
    // MP4 mod tag
    FileHeader();  // dummy constructor to keep valgrind happy
    ~FileHeader();

    bool Allocate(PersistentBitmap *bitMap, int fileSize);  // Initialize a file header,
                                                            //  including allocating space
                                                            //  on disk for the file data
    void Deallocate(PersistentBitmap *bitMap);              // De-allocate this file's
                                                            //  data blocks

    void FetchFrom(int sectorNumber);  // Initialize file header from disk
    void WriteBack(int sectorNumber);  // Write modifications to file header
                                       //  back to disk

    int ByteToSector(int offset);  // Convert a byte offset into the file
                                   // to the disk sector containing
                                   // the byte

    int FileLength();  // Return the length of the file
                       // in bytes

    void Print();  // Print the contents of the file.

   private:
    /*
            MP4 hint:
            You will need a data structure to store more information in a header.
            Fields in a class can be separated into disk part and in-core part.
            Disk part are data that will be written into disk.
            In-core part are data only lies in memory, and are used to maintain the data structure of this class.
            In order to implement a data structure, you will need to add some "in-core" data
            to maintain data structure.

            Disk Part - numBytes, numSectors, dataSectors occupy exactly 128 bytes and will be
            written to a sector on disk.
            In-core part - none

    */

    // MP4 start
    int numBytes;                  // Number of bytes in the file
    int numSectors;                // Number of data sectors in the file
    int dataSectors[NumPointers];  // Disk sector numbers for each data
                                   // block in the file
    void InitLevel();
    enum { LDirect,
           LSingle,
           LDouble,
           LTriple
    } level;
    int levelSectors;
    IndexBlock **nextIndexBlocks;
    // MP4 end
};

#endif  // FILEHDR_H
