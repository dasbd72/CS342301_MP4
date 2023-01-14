// filehdr.cc
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector,
//
//      Unlike in a real system, we do not keep track of file permissions,
//	ownership, last modification date, etc., in the file header.
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "filehdr.h"

#include "copyright.h"
#include "debug.h"
#include "main.h"
#include "synchdisk.h"

IndexBlock::IndexBlock(int level) : level(level) {
    // if (debug->IsEnabled('f'))
    //     printf("IndexBlock::IndexBlock(%d)\n", level);

    numBytes = -1;
    numSectors = -1;
    levelSectors = -1;
    memset(nextSectors, -1, sizeof(nextSectors));
    nextIndexBlocks = NULL;
}
IndexBlock::~IndexBlock() {
    // if (debug->IsEnabled('f'))
    //     printf("IndexBlock::~IndexBlock(), level: %d\n", level);

    if (level != 0) {
        ASSERT(nextIndexBlocks != NULL);
        for (int i = 0; i < levelSectors; i++) {
            ASSERT(nextIndexBlocks[i] != NULL);
            delete nextIndexBlocks[i];
        }
        delete[] nextIndexBlocks;
    }
}
bool IndexBlock::Allocate(PersistentBitmap *freeMap, int remSize) {
    // if (debug->IsEnabled('f'))
    //     printf("IndexBlock::Allocate(%x, %d)\n", freeMap, remSize);

    ASSERT(remSize > 0);

    numBytes = remSize;
    numSectors = divRoundUp(remSize, SectorSize);
    levelSectors = divRoundUp(remSize, sizePerPointer[level]);

    // if (debug->IsEnabled('f')) {
    //     printf("level: %d\n", level);
    //     printf("sizePerPointer: %d\n", sizePerPointer[level]);
    //     printf("levelSectors: %d\n", levelSectors);
    // }

    for (int i = 0; i < levelSectors; i++) {
        nextSectors[i] = freeMap->FindAndSet();
        ASSERT(freeMap->Test((int)nextSectors[i]));
    }

    if (level != 0) {
        nextIndexBlocks = new IndexBlock *[NumSectorInt];
        memset(nextIndexBlocks, 0, sizeof(IndexBlock *) * NumSectorInt);
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i] = new IndexBlock(level - 1);
            if (!nextIndexBlocks[i]->Allocate(freeMap, ((i == levelSectors - 1 && numBytes % sizePerPointer[level]) ? remSize % sizePerPointer[level] : sizePerPointer[level])))
                return FALSE;
        }
    }
    return TRUE;
}
void IndexBlock::Deallocate(PersistentBitmap *freeMap) {
    // if (debug->IsEnabled('f'))
    //     printf("IndexBlock::Deallocate(%x)\n", freeMap);

    if (level != 0) {
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i]->Deallocate(freeMap);
        }
    }

    for (int i = 0; i < levelSectors; i++) {
        ASSERT(freeMap->Test((int)nextSectors[i]));
        freeMap->Clear((int)nextSectors[i]);
    }
}
void IndexBlock::FetchFrom(int sector, int remSize) {
    // if (debug->IsEnabled('f'))
    //     printf("IndexBlock::FetchFrom(%d, %d)\n", sector, remSize);

    numBytes = remSize;
    numSectors = divRoundUp(remSize, SectorSize);
    levelSectors = divRoundUp(remSize, sizePerPointer[level]);
    kernel->synchDisk->ReadSector(sector, (char *)nextSectors);

    if (level != 0) {
        nextIndexBlocks = new IndexBlock *[NumSectorInt];
        memset(nextIndexBlocks, 0, sizeof(IndexBlock *) * NumSectorInt);
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i] = new IndexBlock(level - 1);
            nextIndexBlocks[i]->FetchFrom(nextSectors[i], (i == levelSectors - 1 && numBytes % sizePerPointer[level] ? remSize % sizePerPointer[level] : sizePerPointer[level]));
        }
    }
}
void IndexBlock::WriteBack(int sector) {
    // if (debug->IsEnabled('f'))
    //     printf("IndexBlock::WriteBack(%d), level:%d\n", sector, level);

    kernel->synchDisk->WriteSector(sector, (char *)nextSectors);

    if (level != 0) {
        for (int i = 0; i < levelSectors; i++) {
            if (nextIndexBlocks[i] != NULL) {
                nextIndexBlocks[i]->WriteBack(nextSectors[i]);
            }
        }
    }
}
int IndexBlock::ByteToSector(int offset) {
    // if (debug->IsEnabled('f'))
    //     printf("IndexBlock::ByteToSector(%d)\n", offset);

    ASSERT(offset >= 0);
    ASSERT(offset < levelSectors * sizePerPointer[level]);

    if (level == 0) {
        return nextSectors[offset / sizePerPointer[level]];
    } else {
        int levelSector = offset / sizePerPointer[level];
        return nextIndexBlocks[levelSector]->ByteToSector(offset - levelSector * sizePerPointer[level]);
    }
}
void IndexBlock::PrintSectors() {
    for (int i = 0; i < levelSectors; i++)
        printf("%d ", nextSectors[i]);

    if (level != 0) {
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i]->PrintSectors();
        }
    }
}
void IndexBlock::PrintContents() {
    int i, k, j;
    char *data = new char[SectorSize];

    for (i = k = 0; i < levelSectors; i++) {
        kernel->synchDisk->ReadSector(nextSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
            if ('\040' <= data[j] && data[j] <= '\176')  // isprint(data[j])
                printf("%c", data[j]);
            else
                printf("\\%x", (unsigned char)data[j]);
        }
        printf("\n");
    }
    delete[] data;

    if (level != 0) {
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i]->PrintContents();
        }
    }
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::FileHeader
//	There is no need to initialize a fileheader,
//	since all the information should be initialized by Allocate or FetchFrom.
//	The purpose of this function is to keep valgrind happy.
//----------------------------------------------------------------------
FileHeader::FileHeader() {
    // if (debug->IsEnabled('f'))
    //     printf("FileHeader::FileHeader()\n");

    numBytes = -1;
    numSectors = -1;
    memset(dataSectors, -1, sizeof(dataSectors));
    // MP4 start
    level = LDirect;
    levelSectors = -1;
    nextIndexBlocks = NULL;
    // MP4 end
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::~FileHeader
//	Currently, there is not need to do anything in destructor function.
//	However, if you decide to add some "in-core" data in header
//	Always remember to deallocate their space or you will leak memory
//----------------------------------------------------------------------
FileHeader::~FileHeader() {
    // MP4 start
    // if (debug->IsEnabled('f'))
    //     printf("FileHeader::~FileHeader()\n");

    if (level != LDirect) {
        ASSERT(nextIndexBlocks != NULL);
        for (int i = 0; i < levelSectors; i++) {
            ASSERT(nextIndexBlocks[i] != NULL);
            delete nextIndexBlocks[i];
        }
        delete[] nextIndexBlocks;
    }
    // MP4 end
}

void FileHeader::InitLevel() {
    // MP4 start
    if (numBytes <= MaxDirectBytes) {
        level = LDirect;
    } else if (numBytes <= MaxSingleIndirectBytes) {
        level = LSingle;
    } else if (numBytes <= MaxDoubleIndirectBytes) {
        level = LDouble;
    } else if (numBytes <= MaxTripleIndirectBytes) {
        level = LTriple;
    } else {
        ASSERT((FALSE));  // Not supported file size;
    }
    // MP4 end
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool FileHeader::Allocate(PersistentBitmap *freeMap, int fileSize) {
    // MP4 start
    // if (debug->IsEnabled('f'))
    //     printf("FileHeader::Allocate(%x, %d)\n", freeMap, fileSize);

    numBytes = fileSize;
    numSectors = divRoundUp(fileSize, SectorSize);
    if (freeMap->NumClear() < numSectors)
        return FALSE;  // not enough space

    // for (int i = 0; i < numSectors; i++) {
    //     dataSectors[i] = freeMap->FindAndSet();
    //     // since we checked that there was enough free space,
    //     // we expect this to succeed
    //     ASSERT(dataSectors[i] >= 0);
    // }

    InitLevel();

    levelSectors = divRoundUp(fileSize, sizePerPointer[level]);
    // if (debug->IsEnabled('f')) {
    //     printf("level: %d\n", level);
    //     printf("sizePerPointer: %d\n", sizePerPointer[level]);
    //     printf("levelSectors: %d\n", levelSectors);
    // }

    for (int i = 0; i < levelSectors; i++) {
        dataSectors[i] = freeMap->FindAndSet();
        ASSERT(dataSectors[i] >= 0);
    }

    if (level != LDirect) {
        nextIndexBlocks = new IndexBlock *[NumPointers];
        memset(nextIndexBlocks, 0, sizeof(IndexBlock *) * NumPointers);
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i] = new IndexBlock(level - 1);
            if (!nextIndexBlocks[i]->Allocate(freeMap, ((i == levelSectors - 1 && fileSize % sizePerPointer[level]) ? fileSize % sizePerPointer[level] : sizePerPointer[level])))
                return FALSE;
        }
    }
    // MP4 end
    return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void FileHeader::Deallocate(PersistentBitmap *freeMap) {
    // MP4 start
    // if (debug->IsEnabled('f'))
    //     printf("FileHeader::Deallocate(%x)\n", freeMap);

    // for (int i = 0; i < numSectors; i++) {
    //     ASSERT(freeMap->Test((int)dataSectors[i]));  // ought to be marked!
    //     freeMap->Clear((int)dataSectors[i]);
    // }

    if (level != LDirect) {
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i]->Deallocate(freeMap);
        }
    }
    for (int i = 0; i < levelSectors; i++) {
        ASSERT(freeMap->Test((int)dataSectors[i]));  // ought to be marked!
        freeMap->Clear((int)dataSectors[i]);
    }
    // MP4 end
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void FileHeader::FetchFrom(int sector) {
    // MP4 start
    // if (debug->IsEnabled('f'))
    //     printf("FileHeader::FetchFrom(%d)\n", sector);

    // kernel->synchDisk->ReadSector(sector, (char *)this);

    /*
            MP4 Hint:
            After you add some in-core informations, you will need to rebuild the header's structure
    */
    char buf[FileHeaderDiskSize];
    kernel->synchDisk->ReadSector(sector, buf);
    int offset = 0;
    memcpy(&numBytes, buf + offset, sizeof(numBytes));
    offset += sizeof(numBytes);
    memcpy(&numSectors, buf + offset, sizeof(numSectors));
    offset += sizeof(numSectors);
    memcpy(&dataSectors, buf + offset, sizeof(dataSectors));
    offset += sizeof(dataSectors);

    InitLevel();

    levelSectors = divRoundUp(numBytes, sizePerPointer[level]);
    if (level != LDirect) {
        nextIndexBlocks = new IndexBlock *[NumPointers];
        memset(nextIndexBlocks, 0, sizeof(IndexBlock *) * NumPointers);
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i] = new IndexBlock(level - 1);
            nextIndexBlocks[i]->FetchFrom(dataSectors[i], (i == levelSectors - 1 && numBytes % sizePerPointer[level] ? numBytes % sizePerPointer[level] : sizePerPointer[level]));
        }
    }
    // MP4 end
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk.
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void FileHeader::WriteBack(int sector) {
    // MP4 start
    // if (debug->IsEnabled('f'))
    //     printf("FileHeader::WriteBack(%d)\n", sector);

    // kernel->synchDisk->WriteSector(sector, (char *)this);

    /*
            MP4 Hint:
            After you add some in-core informations, you may not want to write all fields into disk.
            Use this instead:
            char buf[SectorSize];
            memcpy(buf + offset, &dataToBeWritten, sizeof(dataToBeWritten));
            ...
    */
    char buf[FileHeaderDiskSize];
    int offset = 0;
    memcpy(buf + offset, &numBytes, sizeof(numBytes));
    offset += sizeof(numBytes);
    memcpy(buf + offset, &numSectors, sizeof(numSectors));
    offset += sizeof(numSectors);
    memcpy(buf + offset, &dataSectors, sizeof(dataSectors));
    offset += sizeof(dataSectors);
    kernel->synchDisk->WriteSector(sector, buf);

    if (level != LDirect) {
        for (int i = 0; i < levelSectors; i++) {
            if (nextIndexBlocks[i] != NULL) {
                nextIndexBlocks[i]->WriteBack(dataSectors[i]);
            }
        }
    }
    // MP4 end
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int FileHeader::ByteToSector(int offset) {
    // MP4 start
    // if (debug->IsEnabled('f'))
    //     printf("FileHeader::ByteToSector(%d)\n", offset);

    // return (dataSectors[offset / SectorSize]);

    int sec = -1;
    if (level == LDirect) {
        sec = dataSectors[offset / sizePerPointer[level]];
    } else {
        int levelSector = offset / sizePerPointer[level];
        sec = nextIndexBlocks[levelSector]->ByteToSector(offset - levelSector * sizePerPointer[level]);
    }
    return sec;
    // MP4 end
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int FileHeader::FileLength() {
    return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void FileHeader::Print() {
    // MP4 start
    // if (debug->IsEnabled('f'))
    //     printf("FileHeader::Print()\n");

    int i, j, k;
    char *data = new char[SectorSize];

    printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);
    for (i = 0; i < levelSectors; i++)
        printf("%d ", dataSectors[i]);
    if (level != LDirect) {
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i]->PrintSectors();
        }
    }
    printf("\nFile contents:\n");
    for (i = k = 0; i < levelSectors; i++) {
        kernel->synchDisk->ReadSector(dataSectors[i], data);
        for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++) {
            if ('\040' <= data[j] && data[j] <= '\176')  // isprint(data[j])
                printf("%c", data[j]);
            else
                printf("\\%x", (unsigned char)data[j]);
        }
        printf("\n");
    }
    delete[] data;
    if (level != LDirect) {
        for (int i = 0; i < levelSectors; i++) {
            nextIndexBlocks[i]->PrintContents();
        }
    }
    // MP4 end
}
