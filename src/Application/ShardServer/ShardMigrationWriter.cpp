#include "ShardMigrationWriter.h"
#include "Application/Common/ContextTransport.h"
#include "ShardServer.h"
#include "ShardQuorumProcessor.h"

ShardMigrationWriter::ShardMigrationWriter()
{
    onTimeout.SetCallable(MFUNC(ShardMigrationWriter, OnTimeout));
    onTimeout.SetDelay(SHARD_MIGRATION_WRITER_DELAY);
    Reset();
}

ShardMigrationWriter::~ShardMigrationWriter()
{
    if (cursor != NULL)
        delete cursor;
}

void ShardMigrationWriter::Init(ShardServer* shardServer_)
{
    shardServer = shardServer_;
    environment = shardServer->GetDatabaseManager()->GetEnvironment();

    Reset();
}

void ShardMigrationWriter::Reset()
{
    cursor = NULL;
//    asyncCursor = NULL;
    isActive = false;
    sendFirst = false;
    quorumProcessor = NULL;
    bytesSent = 0;
    bytesTotal = 0;
    startTime = 0;
    prevBytesSent = 0;
    EventLoop::Remove(&onTimeout);
}

bool ShardMigrationWriter::IsActive()
{
    return isActive;
}

uint64_t ShardMigrationWriter::GetShardID()
{
    return shardID;
}

uint64_t ShardMigrationWriter::GetQuorumID()
{
    return quorumID;
}

uint64_t ShardMigrationWriter::GetNodeID()
{
    return nodeID;
}

uint64_t ShardMigrationWriter::GetBytesSent()
{
    return bytesSent;
}

uint64_t ShardMigrationWriter::GetBytesTotal()
{
    return bytesTotal;
}

uint64_t ShardMigrationWriter::GetThroughput()
{
    uint64_t now;
    
    now = NowClock();
    
    if (now > startTime)
        return bytesSent / ((now - startTime)/1000.0);
    else
        return 0;
}

void ShardMigrationWriter::Begin(ClusterMessage& request)
{
    ConfigState*        configState;
    ConfigShard*        configShard;
    ConfigShardServer*  configShardServer;
    
    ASSERT(!isActive);
    ASSERT(cursor == NULL);

    configState = shardServer->GetConfigState();
    configShard = configState->GetShard(request.shardID);
    ASSERT(configShard != NULL);
    configShardServer = configState->GetShardServer(request.nodeID);
    ASSERT(configShardServer != NULL);
    
    quorumProcessor = shardServer->GetQuorumProcessor(configShard->quorumID);
    ASSERT(quorumProcessor != NULL);
    
    isActive = true;
    nodeID = request.nodeID;
    quorumID = request.quorumID;
    shardID = request.shardID;
    
    bytesTotal = environment->GetSize(QUORUM_DATABASE_DATA_CONTEXT, shardID);
    startTime = NowClock();
    
    CONTEXT_TRANSPORT->AddNode(nodeID, configShardServer->endpoint);
    
    Log_Debug("ShardMigrationWriter::Begin() nodeID = %U", nodeID);
    Log_Debug("ShardMigrationWriter::Begin() quorumID = %U", quorumID);
    Log_Debug("ShardMigrationWriter::Begin() shardID = %U", shardID);

    Log_Message("Migrating shard %U into quorum %U (sending)", shardID, quorumID);

    sendFirst = true;
    EventLoop::Add(&onTimeout);
    CONTEXT_TRANSPORT->RegisterWriteReadyness(nodeID, MFUNC(ShardMigrationWriter, OnWriteReadyness));
}

void ShardMigrationWriter::Abort()
{
    Log_Message("Aborting shard migration...", shardID, quorumID);
    
    CONTEXT_TRANSPORT->UnregisterWriteReadyness(nodeID, MFUNC(ShardMigrationWriter, OnWriteReadyness));
    Reset();

    if (cursor != NULL)
    {
        delete cursor;
        cursor = NULL;
    }
}

//void ShardMigrationWriter::OnResult()
//{
//    StorageAsyncBulkResult* result;
//    StorageFileKeyValue*    kv;
//    
//    result = asyncCursor->GetLastResult();
//    if (!result)
//    {
//        // asyncCursor automatically deletes itself after the last result
//        asyncCursor = NULL;
//        SendCommit();
//        return;
//    }
//    
//    FOREACH (kv, result->dataPage)
//    {
//        SendItem(kv);
//    }
//}

void ShardMigrationWriter::SendFirst()
{
    ClusterMessage      msg;
    ReadBuffer          key;
    ReadBuffer          value;

    ASSERT(cursor == NULL);
    cursor = environment->GetBulkCursor(QUORUM_DATABASE_DATA_CONTEXT, shardID);

    msg.ShardMigrationBegin(quorumID, shardID);
    CONTEXT_TRANSPORT->SendClusterMessage(nodeID, msg);

    Log_Debug("ShardMigrationWriter sending BEGIN");

    // send first KV
    kv = cursor->First();
    if (kv)
        SendItem(kv);
    else
        SendCommit();
}

void ShardMigrationWriter::SendNext()
{
    ASSERT(cursor != NULL);
    kv = cursor->Next(kv);
    if (kv)
        SendItem(kv);
    else
        SendCommit();
}

void ShardMigrationWriter::SendCommit()
{
    ClusterMessage msg;
    
    msg.ShardMigrationCommit(quorumID, shardID);
    CONTEXT_TRANSPORT->SendClusterMessage(nodeID, msg);

    Log_Message("Finished sending shard %U...", shardID);

    if (cursor != NULL)
    {
        delete cursor;
        cursor = NULL;
    }

    CONTEXT_TRANSPORT->UnregisterWriteReadyness(nodeID, MFUNC(ShardMigrationWriter, OnWriteReadyness));

    Reset();
}

void ShardMigrationWriter::SendItem(StorageKeyValue* kv)
{
    ClusterMessage msg;
    
    if (kv->GetType() == STORAGE_KEYVALUE_TYPE_SET)
    {
        msg.ShardMigrationSet(quorumID, shardID, kv->GetKey(), kv->GetValue());
        bytesSent += kv->GetKey().GetLength() + kv->GetValue().GetLength();
    }
    else
    {
        msg.ShardMigrationDelete(quorumID, shardID, kv->GetKey());
        bytesSent += kv->GetKey().GetLength();
    }

    CONTEXT_TRANSPORT->SendClusterMessage(nodeID, msg);
}

void ShardMigrationWriter::OnWriteReadyness()
{
    uint64_t bytesBegin;

    ASSERT(quorumProcessor != NULL);
    if (!quorumProcessor->IsPrimary()
     || !shardServer->GetConfigState()->isMigrating
     || (shardServer->GetConfigState()->isMigrating &&
         (shardServer->GetConfigState()->migrateShardID != shardID ||
          shardServer->GetConfigState()->migrateQuorumID != quorumID)))
    {
        Abort();
        return;
    }

    if (sendFirst)
    {
        sendFirst = false;
        SendFirst();
    }
    else
    {
        bytesBegin = bytesSent;

        while (bytesSent < bytesBegin + SHARD_MIGRATION_WRITER_GRAN)
        {
            if (!cursor)
                break;
            SendNext();
        }
    }
}

void ShardMigrationWriter::OnTimeout()
{
    ConfigState*    configState;
    ConfigQuorum*   configQuorum;

    ASSERT(quorumProcessor != NULL);
    if (!quorumProcessor->IsPrimary()
     || !shardServer->GetConfigState()->isMigrating
     || (shardServer->GetConfigState()->isMigrating &&
         (shardServer->GetConfigState()->migrateShardID != shardID ||
          shardServer->GetConfigState()->migrateQuorumID != quorumID)))
    {
        Abort();
        return;
    }
    
    // check the destination nodeID is still in the quorum
    configState = quorumProcessor->GetShardServer()->GetConfigState();
    configQuorum = configState->GetQuorum(quorumID);
    if (configQuorum == NULL)
    {
        Abort();
        return;
    }
    if (!configQuorum->IsMember(nodeID))
    {
        Abort();
        return;
    }

    if (bytesSent == prevBytesSent)
    {
        Abort();
        return;
    }

    prevBytesSent = bytesSent;
    EventLoop::Add(&onTimeout);
}
