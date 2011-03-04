#ifndef STORAGEMEMOCHUNKLISTER_H
#define STORAGEMEMOCHUNKLISTER_H

#include "StorageChunkLister.h"
#include "StorageDataPage.h"
#include "StorageMemoChunk.h"

/*
===============================================================================================
 
StorageMemoChunkLister
 
===============================================================================================
*/

class StorageMemoChunkLister : public StorageChunkLister
{
public:
    StorageMemoChunkLister();
    
    void                    Init(StorageMemoChunk* chunk, ReadBuffer& startKey, 
                                 unsigned count, unsigned offset);

    void                    Load();
    
    StorageFileKeyValue*    First(ReadBuffer& firstKey);
    StorageFileKeyValue*    Next(StorageFileKeyValue*);
    
    uint64_t                GetNumKeys();
    

private:
    StorageDataPage         dataPage;
};

#endif
