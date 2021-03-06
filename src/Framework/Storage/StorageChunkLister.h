#ifndef STORAGECHUNKLISTER_H
#define STORAGECHUNKLISTER_H

#include "System/Platform.h"

class StorageFileKeyValue;
class ReadBuffer;

/*
===============================================================================================
 
StorageChunkLister
 
===============================================================================================
*/

class StorageChunkLister
{
public:
    virtual ~StorageChunkLister() {}

    virtual void                    SetDirection(bool forwardDirection) = 0;
    virtual StorageFileKeyValue*    First(ReadBuffer& startKey) = 0;
    virtual StorageFileKeyValue*    Next(StorageFileKeyValue*) = 0;
    
    virtual uint64_t                GetNumKeys() = 0;

};

#endif
