#include "StorageAsyncBulkCursor.h"
#include "StorageChunkReader.h"
#include "StorageEnvironment.h"
#include "StorageMemoChunkLister.h"
#include "StorageUnwrittenChunkLister.h"
#include "System/Events/Callable.h"
#include "System/IO/IOProcessor.h"

#define MAX_PRELOAD_THRESHOLD   1*MB

/*
===============================================================================================

 StorageAsyncBulkResult

===============================================================================================
*/

StorageAsyncBulkResult::StorageAsyncBulkResult(StorageAsyncBulkCursor* cursor_) :
 dataPage(NULL, 0)
{
    cursor = cursor_;
    last = false;
}

void StorageAsyncBulkResult::OnComplete()
{
    bool    last;
    
    last = false;
    if (cursor->lastResult == NULL)
        last = true;

    cursor->lastResult = this;
    Call(onComplete);
    cursor->lastResult = NULL;    
    delete this;
    
    // automatically delete the cursor after the last result
    if (last)
        delete cursor;
}

/*
===============================================================================================

 StorageAsyncBulkCursor

===============================================================================================
*/

StorageAsyncBulkCursor::StorageAsyncBulkCursor()
{
    isAborted = false;
    env = NULL;
    shard = NULL;
    lastResult = NULL;
    itChunk = NULL;
    threadPool = NULL;
}

void StorageAsyncBulkCursor::SetEnvironment(StorageEnvironment* env_)
{
    env = env_;
}

void StorageAsyncBulkCursor::SetShard(uint64_t contextID, uint64_t shardID)
{
    shard = env->GetShard(contextID, shardID);
    ASSERT(shard);
}

void StorageAsyncBulkCursor::SetThreadPool(ThreadPool* threadPool_)
{
    threadPool = threadPool_;
}

void StorageAsyncBulkCursor::SetOnComplete(Callable onComplete_)
{
    onComplete = onComplete_;
}

StorageAsyncBulkResult* StorageAsyncBulkCursor::GetLastResult()
{
    return lastResult;
}

void StorageAsyncBulkCursor::OnNextChunk()
{
    ReadBuffer                  startKey;
    ReadBuffer                  endKey;
    ReadBuffer                  prefix;
    StorageFileChunk*           fileChunk;
    StorageMemoChunk*           memoChunk;
    StorageAsyncBulkResult*     result;
    StorageMemoChunkLister      memoLister;
    StorageUnwrittenChunkLister unwrittenLister;
    
    if (itChunk == NULL)
        itChunk = shard->GetChunks().First();
    else
        itChunk = shard->GetChunks().Next(itChunk);
    
    if (itChunk == NULL)
    {
        lastResult = NULL;
        Call(onComplete);
        return;
    }

    if ((*itChunk)->GetChunkState() == StorageChunk::Written)
    {
        fileChunk = (StorageFileChunk*) (*itChunk);
        chunkName = fileChunk->GetFilename();
        threadPool->Execute(MFUNC(StorageAsyncBulkCursor, AsyncReadFileChunk));
    }
    else if ((*itChunk)->GetChunkState() == StorageChunk::Unwritten)
    {
        fileChunk = (StorageFileChunk*) (*itChunk);
        unwrittenLister.Init(*fileChunk, startKey, prefix, 0, true);
        result = new StorageAsyncBulkResult(this);
        result->dataPage = *unwrittenLister.GetDataPage();
        result->onComplete = onComplete;
        lastResult = result;
        // direct callback, maybe yieldTimer would be better
        result->OnComplete();        
    }
    else if ((*itChunk)->GetChunkState() == StorageChunk::Serialized)
    {
        memoChunk = (StorageMemoChunk*) (*itChunk);
        memoLister.Init(memoChunk, startKey, endKey, prefix, 0, false, true);
        result = new StorageAsyncBulkResult(this);
        result->dataPage = *memoLister.GetDataPage();
        result->onComplete = onComplete;
        lastResult = result;
        // direct callback, maybe yieldTimer would be better
        result->OnComplete();
    }
    else
    {
        // memoChunk
        // TODO: serialize memoChunk and suspend write operations
    }
}

void StorageAsyncBulkCursor::Abort()
{
    isAborted = true;
}

// this runs in async thread
void StorageAsyncBulkCursor::AsyncReadFileChunk()
{
    StorageChunkReader      reader;
    StorageDataPage*        dataPage;
    StorageAsyncBulkResult* result;
    Callable                onNextChunk = MFUNC(StorageAsyncBulkCursor, OnNextChunk);
    
    reader.Open(chunkName, MAX_PRELOAD_THRESHOLD);
    
    lastResult = NULL;
    result = new StorageAsyncBulkResult(this);
    dataPage = reader.FirstDataPage();
    
    while (dataPage != NULL)
    {
        if (isAborted || env->shuttingDown)
        {
            // abort cursor
            delete result;
            delete this;
            return;
        }
    
        TransferDataPage(result, dataPage);
        OnResult(result);
        
        result = new StorageAsyncBulkResult(this);
        dataPage = reader.NextDataPage();
    }
    
    OnResult(result);
    IOProcessor::Complete(&onNextChunk);
}

void StorageAsyncBulkCursor::TransferDataPage(StorageAsyncBulkResult* result, 
 StorageDataPage* dataPage)
{
    // TODO: zero-copy
    result->dataPage = *dataPage;
}

void StorageAsyncBulkCursor::OnResult(StorageAsyncBulkResult* result)
{
    Callable    callable;
    
    result->onComplete = onComplete;
    callable = MFUNC_OF(StorageAsyncBulkResult, OnComplete, result);
    IOProcessor::Complete(&callable);
}
