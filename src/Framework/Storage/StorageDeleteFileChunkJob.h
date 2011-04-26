#ifndef STORAGEDELETEFILECHUNKJOB_H
#define STORAGEDELETEFILECHUNKJOB_H

#include "System/Threading/Job.h"

class StorageFileChunk;

/*
===============================================================================================

 StorageDeleteFileChunkJob

===============================================================================================
*/

class StorageDeleteFileChunkJob : public Job
{
public:
    StorageDeleteFileChunkJob(StorageFileChunk* chunk);
    
    void                Execute();
    void                OnComplete();

private:
    StorageFileChunk*   chunk;
};

#endif
