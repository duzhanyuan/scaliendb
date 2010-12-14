#include "StorageEnvironment.h"
#include "System/Events/EventLoop.h"
#include "System/FileSystem.h"
#include "StorageChunkSerializer.h"
#include "StorageEnvironmentWriter.h"

StorageEnvironment::StorageEnvironment()
{
    commitThread = NULL;
    serializerThread = NULL;
    writerThread = NULL;

    onChunkSerialize = MFUNC(StorageEnvironment, OnChunkSerialize);
    onChunkWrite = MFUNC(StorageEnvironment, OnChunkWrite);

    nextChunkID = 1;
    nextLogSegmentID = 1;
    
    asyncCommit = false;
}

bool StorageEnvironment::Open(Buffer& envPath_)
{
    char    lastChar;
    Buffer  tmp;

    commitThread = ThreadPool::Create(1);
    commitThread->Start();
    
    serializerThread = ThreadPool::Create(1);
    serializerThread->Start();

    writerThread = ThreadPool::Create(1);
    writerThread->Start();

    envPath.Write(envPath_);
    lastChar = envPath.GetCharAt(envPath.GetLength() - 1);
    if (lastChar != '/' && lastChar != '\\')
        envPath.Append('/');

    chunkPath.Write(envPath);
    chunkPath.Append("chunks/");

    logPath.Write(envPath);
    logPath.Append("logs/");

    tmp.Write(envPath);
    tmp.NullTerminate();
    FS_CreateDir(tmp.GetBuffer());

    tmp.Write(chunkPath);
    tmp.NullTerminate();
    FS_CreateDir(tmp.GetBuffer());

    tmp.Write(logPath);
    tmp.NullTerminate();
    FS_CreateDir(tmp.GetBuffer());
    
    // TODO: open 'toc' or 'toc.new' file

    logSegmentWriter = new StorageLogSegment;
    tmp.Write(logPath);
    tmp.Appendf("log.%020U", nextLogSegmentID);
    logSegmentWriter->Open(tmp, nextLogSegmentID);
    nextLogSegmentID++;

    return true;
}

void StorageEnvironment::Close()
{
    commitThread->Stop();
    serializerThread->Stop();
    writerThread->Stop();
}

void StorageEnvironment::SetStorageConfig(StorageConfig& config_)
{
    config = config_;
}

uint64_t StorageEnvironment::GetShardID(uint16_t contextID, uint64_t tableID, ReadBuffer& key)
{
    StorageShard* it;

    FOREACH(it, shards)
    {
        if (it->GetContextID() == contextID && it->GetTableID() == tableID)
        {
            if (it->RangeContains(key))
                return it->GetShardID();
        }
    }

    return 0;
}

bool StorageEnvironment::Get(uint16_t contextID, uint64_t shardID, ReadBuffer key, ReadBuffer& value)
{
    StorageShard*       shard;
    StorageChunk*       chunk;
    StorageChunk**      itChunk;
    StorageKeyValue*    kv;

    shard = GetShard(contextID, shardID);
    if (shard == NULL)
        return false;

    chunk = shard->GetMemoChunk();
    kv = chunk->Get(key);
    if (kv != NULL)
    {
        if (kv->GetType() == STORAGE_KEYVALUE_TYPE_DELETE)
        {
            return false;
        }
        else if (kv->GetType() == STORAGE_KEYVALUE_TYPE_SET)
        {
            value = kv->GetValue();
            return true;
        }
        else
            ASSERT_FAIL();
    }

    BFOREACH(itChunk, shard->GetChunks())
    {
        kv = (*itChunk)->Get(key);
        if (kv != NULL)
        {
            if (kv->GetType() == STORAGE_KEYVALUE_TYPE_DELETE)
            {
                return false;
            }
            else if (kv->GetType() == STORAGE_KEYVALUE_TYPE_SET)
            {
                value = kv->GetValue();
                return true;
            }
            else
                ASSERT_FAIL();
        }
    }

    return false;
}

bool StorageEnvironment::Set(uint16_t contextID, uint64_t shardID, ReadBuffer key, ReadBuffer value)
{
    uint64_t            commandID;
    StorageShard*       shard;
    StorageMemoChunk*   chunk;
    
    if (commitThread->GetNumActive() > 0)
        return false;

    shard = GetShard(contextID, shardID);
    if (shard == NULL)
        return false;

    commandID = logSegmentWriter->AppendSet(contextID, shardID, key, value);
    if (commandID < 0)
        return false;

    chunk = shard->GetMemoChunk();
    assert(chunk != NULL);
    if (!chunk->Set(key, value))
    {
        logSegmentWriter->Undo();
        return false;
    }
    chunk->RegisterLogCommand(logSegmentWriter->GetLogSegmentID(), commandID);

    return true;
}

bool StorageEnvironment::Delete(uint16_t contextID, uint64_t shardID, ReadBuffer key)
{
    uint32_t            commandID;
    StorageShard*       shard;
    StorageMemoChunk*   chunk;

    if (commitThread->GetNumActive() > 0)
        return false;

    shard = GetShard(contextID, shardID);
    if (shard == NULL)
        return false;

    commandID = logSegmentWriter->AppendDelete(contextID, shardID, key);
    if (commandID < 0)
        return false;

    chunk = shard->GetMemoChunk();
    assert(chunk != NULL);
    if (!chunk->Delete(key))
    {
        logSegmentWriter->Undo();
        return false;
    }
    chunk->RegisterLogCommand(logSegmentWriter->GetLogSegmentID(), commandID);

    return true;
}

void StorageEnvironment::SetOnCommit(Callable& onCommit_)
{
    onCommit = onCommit_;
    asyncCommit = true;
}

bool StorageEnvironment::Commit()
{
    if (asyncCommit)
    {
        if (commitThread->GetNumActive() > 0)
            return false;

        MFunc<StorageLogSegment, &StorageLogSegment::Commit> callable(logSegmentWriter);        
        commitThread->Execute(callable);
    }
    else
    {
        logSegmentWriter->Commit();
        OnCommit();
    }

    return true;
}

bool StorageEnvironment::GetCommitStatus()
{
    return logSegmentWriter->GetCommitStatus();
}

StorageShard* StorageEnvironment::GetShard(uint16_t contextID, uint64_t shardID)
{
    StorageShard* it;

    FOREACH(it, shards)
    {
        if (it->GetContextID() == contextID && it->GetShardID() == shardID)
            return it;
    }

    return NULL;
}

void StorageEnvironment::CreateShard(uint16_t contextID, uint64_t shardID, uint64_t tableID,
 ReadBuffer firstKey, ReadBuffer lastKey, bool useBloomFilter)
{
    StorageShard*       shard;
    StorageMemoChunk*   memoChunk;

    shard = GetShard(contextID, shardID);
    if (shard != NULL)
        return;

    shard = new StorageShard;
    shard->SetContextID(contextID);
    shard->SetTableID(tableID);
    shard->SetShardID(shardID);
    shard->SetFirstKey(firstKey);
    shard->SetLastKey(lastKey);
    shard->SetUseBloomFilter(useBloomFilter);

    memoChunk = new StorageMemoChunk;
    memoChunk->SetChunkID(nextChunkID++);
    memoChunk->SetUseBloomFilter(useBloomFilter);

    shard->PushMemoChunk(memoChunk);

    shards.Append(shard);
    
    WriteTOC();
}

void StorageEnvironment::DeleteShard(uint16_t /*contextID*/, uint64_t /*shardID*/)
{
//    StorageShard* shard;
//
//    shard = GetShard(shardID);
//    if (shard == NULL)
//        return;
//
//    // TODO: decrease reference counts
//    shards.Delete(shard);
}

void StorageEnvironment::OnCommit()
{
    TryFinalizeLogSegment();
    TrySerializeChunks();
}

void StorageEnvironment::TryFinalizeLogSegment()
{
    Buffer tmp;

    if (logSegmentWriter->GetOffset() < config.logSegmentSize)
        return;

    logSegmentWriter->Close();

    delete logSegmentWriter; // this is wrong
// TODO:    logSegments.Append(logSegmentWriter);

    logSegmentWriter = new StorageLogSegment;
    tmp.Write(logPath);
    tmp.Appendf("log.%020U", nextLogSegmentID);
    logSegmentWriter->Open(tmp, nextLogSegmentID);
    nextLogSegmentID++;
}

void StorageEnvironment::TrySerializeChunks()
{
    Buffer                      tmp;
    StorageShard*               itShard;
    StorageMemoChunk*           memoChunk;
    StorageFileChunk*           fileChunk;
    StorageChunk**              itChunk;
    StorageJob*                 job;

    // look for memoChunks which have been serialized already
    FOREACH(itShard, shards)
    {
        for (itChunk = itShard->GetChunks().First(); itChunk != NULL; /* advanced in body */)
        {
            if ((*itChunk)->GetChunkState() == StorageChunk::Serialized)
            {
                memoChunk = (StorageMemoChunk*) (*itChunk);
                fileChunk = memoChunk->RemoveFileChunk();
                if (fileChunk == NULL)
                    goto Advance;
                itShard->OnChunkSerialized(memoChunk, fileChunk);
                Log_Message("Deleting MemoChunk...");
                delete memoChunk;
                
                tmp.Write(chunkPath);
                tmp.Appendf("chunk.%020U", fileChunk->GetChunkID());
                fileChunk->SetFilename(tmp);
                fileChunks.Append(fileChunk);

                itChunk = itShard->GetChunks().First();
                continue;
            }
Advance:
            itChunk = itChunk = itShard->GetChunks().Next(itChunk);
        }
    }

    if (serializerThread->GetNumActive() > 0)
        return;

    FOREACH(itShard, shards)
    {
        memoChunk = itShard->GetMemoChunk();
//        if (memoChunk->GetLogSegmentID() == logSegmentWriter->GetLogSegmentID())
//            continue;
        if (memoChunk->GetSize() > config.chunkSize)
        {
            job = new StorageSerializeChunkJob(memoChunk, &onChunkSerialize);
            StartJob(serializerThread, job);

            memoChunk = new StorageMemoChunk;
            memoChunk->SetChunkID(nextChunkID++);
            memoChunk->SetUseBloomFilter(itShard->UseBloomFilter());

            itShard->PushMemoChunk(memoChunk);

            return;
        }
    }
}

void StorageEnvironment::TryWriteChunks()
{
    StorageFileChunk*   it;
    StorageJob*         job;

    FOREACH(it, fileChunks)
    {
        if (it->GetChunkState() == StorageChunk::Unwritten)
        {
            job = new StorageWriteChunkJob(it, &onChunkWrite);
            StartJob(writerThread, job);
            return;
        }
    }
}

void StorageEnvironment::OnChunkSerialize()
{
    TrySerializeChunks();
    TryWriteChunks();
}


void StorageEnvironment::OnChunkWrite()
{
    WriteTOC();
    TryWriteChunks();
}

bool StorageEnvironment::IsWriteActive()
{
    return (commitThread->GetNumActive() > 0);
}

void StorageEnvironment::StartJob(ThreadPool* thread, StorageJob* job)
{
    MFunc<StorageJob, &StorageJob::Execute> callable(job);        
    thread->Execute(callable);
}

void StorageEnvironment::WriteTOC()
{
    StorageEnvironmentWriter writer;

    writer.Write(this);
}
