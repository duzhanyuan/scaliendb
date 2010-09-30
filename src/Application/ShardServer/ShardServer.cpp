#include "ShardServer.h"
#include "System/Config.h"
#include "Framework/Replication/ReplicationConfig.h"
#include "Application/Common/ContextTransport.h"

#define REQUEST_TIMEOUT 1000
#define DATABASE_NAME   "system"

static size_t Hash(uint64_t h)
{
    return h;
}

void ShardServer::Init()
{
    unsigned        numControllers;
    uint64_t        nodeID;
    uint64_t        runID;
    const char*     str;
    Endpoint        endpoint;

    configState = NULL;
    
    databaseDir = configFile.GetValue("database.dir", "db");
    systemDatabase.Open(databaseDir, DATABASE_NAME);
    REPLICATION_CONFIG->Init(systemDatabase.GetTable("system"));

    runID = REPLICATION_CONFIG->GetRunID();
    runID += 1;
    REPLICATION_CONFIG->SetRunID(runID);
    REPLICATION_CONFIG->Commit();

    if (REPLICATION_CONFIG->GetNodeID() > 0)
    {
        awaitingNodeID = false;
        CONTEXT_TRANSPORT->SetSelfNodeID(REPLICATION_CONFIG->GetNodeID());
    }
    else
        awaitingNodeID = true;
    
    // connect to the controller nodes
    numControllers = (unsigned) configFile.GetListNum("controllers");
    for (nodeID = 0; nodeID < numControllers; nodeID++)
    {
        str = configFile.GetListValue("controllers", nodeID, "");
        endpoint.Set(str);
        CONTEXT_TRANSPORT->AddNode(nodeID, endpoint);
        controllers.Append(nodeID);
        // this will cause the node to connect to the controllers
        // and if my nodeID is not set the MASTER will automatically send
        // me a SetNodeID cluster message
    }

    CONTEXT_TRANSPORT->SetClusterContext(this);
        
    requestTimer.SetDelay(REQUEST_TIMEOUT);
    requestTimer.SetCallable(MFUNC(ShardServer, OnRequestLeaseTimeout));    
}

bool ShardServer::IsLeaderKnown(uint64_t quorumID)
{
    QuorumData*     quorumData;
    ConfigQuorum*   configQuorum;
    
    quorumData = LocateQuorum(quorumID);
    if (quorumData == NULL)
        return false;

    if (quorumData->isPrimary)
        return true;
    
    configQuorum = configState->GetQuorum(quorumID);
    if (configQuorum == NULL)
        return false;
    
    if (!configQuorum->hasPrimary)
        return false;
    
    // we already checked this case in quorumData earlier
    if (configQuorum->primaryID == REPLICATION_CONFIG->GetNodeID())
        return false;
    
    return true;
}

bool ShardServer::IsLeader(uint64_t quorumID)
{
    QuorumData*     quorumData;
    
    quorumData = LocateQuorum(quorumID);
    if (quorumData == NULL)
        return false;
    
    return quorumData->isPrimary;
}

uint64_t ShardServer::GetLeader(uint64_t quorumID)
{
    QuorumData*     quorumData;
    ConfigQuorum*   configQuorum;
    
    quorumData = LocateQuorum(quorumID);
    if (quorumData == NULL)
        return 0;

    if (quorumData->isPrimary)
        return REPLICATION_CONFIG->GetNodeID();
    
    configQuorum = configState->GetQuorum(quorumID);
    if (configQuorum == NULL)
        return 0;
    
    if (!configQuorum->hasPrimary)
        return 0;
    
    // we already checked this case in quorumData earlier
    if (configQuorum->primaryID == REPLICATION_CONFIG->GetNodeID())
        return 0;
    
    return configQuorum->primaryID;
}

ShardServer::QuorumList* ShardServer::GetQuorums()
{
    return &quorums;
}

void ShardServer::OnAppend(uint64_t quorumID, DataMessage& message, bool ownAppend)
{
    QuorumData*     quorumData;
    StorageTable*   table;
    ClientRequest*  request;
    
    quorumData = LocateQuorum(quorumID);
    if (!quorumData)
        ASSERT_FAIL();
        
    table = LocateTable(message.tableID);
    if (!table)
        ASSERT_FAIL();
    
    request = NULL;
    if (ownAppend)
    {
        assert(quorumData->dataMessages.GetLength() > 0);
        assert(quorumData->dataMessages.First()->type == message.type);
        assert(quorumData->requests.GetLength() > 0);
        request = quorumData->requests.First();
    }
    
    switch (message.type)
    {
    case CLIENTREQUEST_SET:
        if (!table->Set(message.key, message.value))
            ASSERT_FAIL();
        break;
    case CLIENTREQUEST_DELETE:
        table->Delete(message.key); // TODO: return status
        break;
    default:
        ASSERT_FAIL();
        break;
    }

    if (ownAppend)
    {
        if (request)
        {
            // TODO: fix responses
            request->response.OK();
            request->OnComplete();
        }
        
        quorumData->dataMessages.Delete(quorumData->dataMessages.First());
        quorumData->requests.Delete(quorumData->requests.First());
    }
}

bool ShardServer::IsValidClientRequest(ClientRequest* request)
{
     return request->IsShardServerRequest();
}

void ShardServer::OnClientRequest(ClientRequest* request)
{
    DataMessage*    message;
    ConfigShard*    shard;
    QuorumData*     quorumData;
    
    shard = configState->GetShard(request->tableID, ReadBuffer(request->key));
    if (!shard)
    {
        request->response.Failed();
        request->OnComplete();
        return;
    }

    quorumData = LocateQuorum(shard->quorumID);
    if (!quorumData)
    {
        request->response.NoService();
        request->OnComplete();
        return;
    }
    
    if (request->type == CLIENTREQUEST_GET)
        return OnClientRequestGet(request);

    message = new DataMessage;
    FromClientRequest(request, message);
    
    quorumData->requests.Append(request);
    quorumData->dataMessages.Append(message);
    TryAppend(quorumData);
}

void ShardServer::OnClientClose(ClientSession* /*session*/)
{
    // nothing
}

void ShardServer::OnClusterMessage(uint64_t /*nodeID*/, ClusterMessage& msg)
{
    Log_Trace();
    
    switch (msg.type)
    {
        case CLUSTERMESSAGE_SET_NODEID:
            if (!awaitingNodeID)
                return;
            awaitingNodeID = false;
            CONTEXT_TRANSPORT->SetSelfNodeID(msg.nodeID);
            REPLICATION_CONFIG->SetNodeID(msg.nodeID);
            REPLICATION_CONFIG->Commit();
            Log_Trace("my nodeID is %" PRIu64 "", msg.nodeID);
            break;
        case CLUSTERMESSAGE_SET_CONFIG_STATE:
            OnSetConfigState(msg.configState);
            msg.configState = NULL;
            Log_Trace("got new configState, master is %d", 
             configState->hasMaster ? (int) configState->masterID : -1);
            break;
        case CLUSTERMESSAGE_RECEIVE_LEASE:
            OnReceiveLease(msg.quorumID, msg.proposalID);
            Log_Trace("recieved lease, quorumID = %" PRIu64 ", proposalID =  %" PRIu64,
             msg.quorumID, msg.proposalID);
            break;
        default:
            ASSERT_FAIL();
    }
}

void ShardServer::OnIncomingConnectionReady(uint64_t /*nodeID*/, Endpoint /*endpoint*/)
{
    // nothing
}

void ShardServer::OnAwaitingNodeID(Endpoint /*endpoint*/)
{
    // nothing
}

QuorumData* ShardServer::LocateQuorum(uint64_t quorumID)
{
    QuorumData* quorum;
    
    for (quorum = quorums.First(); quorum != NULL; quorums.Next(quorum))
    {
        if (quorum->quorumID == quorumID)
            return quorum;
    }
    
    return NULL;
}

void ShardServer::TryAppend(QuorumData* quorumData)
{
    if (!quorumData->context.IsAppending())
        quorumData->context.Append(quorumData->dataMessages.First());
}

void ShardServer::FromClientRequest(ClientRequest* request, DataMessage* message)
{
    switch (request->type)
    {
        case CLIENTREQUEST_SET:
            message->type = DATAMESSAGE_SET;
            message->tableID = request->tableID;
            message->key.Wrap(request->key);
            message->value.Wrap(request->value);
            return;
        case CLIENTREQUEST_DELETE:
            message->type = DATAMESSAGE_DELETE;
            message->tableID = request->tableID;
            message->key.Wrap(request->key);
            return;
        default:
            ASSERT_FAIL();
    }
}

void ShardServer::OnRequestLeaseTimeout()
{
    QuorumData*     quorum;
    ClusterMessage  msg;
    uint64_t        nodeID;
    uint64_t        *nit;
    
    nodeID = REPLICATION_CONFIG->GetNodeID();
    for (quorum = quorums.First(); quorum != NULL; quorum = quorums.Next(quorum))
    {
        quorum->proposalID = REPLICATION_CONFIG->NextProposalID(quorum->proposalID);
        msg.RequestLease(nodeID, quorum->quorumID, quorum->proposalID, PAXOSLEASE_MAX_LEASE_TIME);
        
        // send to all controllers
        for (nit = controllers.First(); nit != NULL; nit = controllers.Next(nit))
            CONTEXT_TRANSPORT->SendClusterMessage(*nit, msg);

        quorum->leaseTimeout.SetExpireTime(EventLoop::Now() + PAXOSLEASE_MAX_LEASE_TIME);
    }
}

void ShardServer::OnPrimaryLeaseTimeout()
{
    uint64_t        now;
    QuorumData*     quorum;
    
    now = EventLoop::Now();

    for (quorum = quorums.First(); quorum != NULL; quorum = quorums.Next(quorum))
    {
        if (quorum->leaseTimeout.GetExpireTime() > now)
            continue;

        quorum->isPrimary = false;
        EventLoop::Remove(&quorum->leaseTimeout);
    }
}

void ShardServer::OnSetConfigState(ConfigState* configState_)
{
    QuorumData*             quorum;
    QuorumData*             next;
    ConfigQuorum*           configQuorum;
    ConfigQuorum::NodeList* nodes;
    uint64_t*               nit;
    uint64_t                nodeID;
    bool                    found;

    delete configState;
    configState = configState_;
    
    nodeID = REPLICATION_CONFIG->GetNodeID();

    // look for removal
    for (quorum = quorums.First(); quorum != NULL; quorum = next)
    {
        configQuorum = configState->GetQuorum(quorum->quorumID);
        if (configQuorum == NULL)
        {
            // TODO: quorum has disappeared?
            ASSERT_FAIL();
        }
        
        found = false;
        next = quorums.Next(quorum);
        
        nodes = &configQuorum->activeNodes;
        for (nit = nodes->First(); nit != NULL; nodes->Next(nit))
        {
            if (*nit == nodeID)
            {
                found = true;
                break;
            }
        }
        if (found)
            continue;

        nodes = &configQuorum->inactiveNodes;
        for (nit = nodes->First(); nit != NULL; nodes->Next(nit))
        {
            if (*nit == nodeID)
            {
                found = true;
                break;
            }
        }
        if (found)
            continue;
        
        next = quorums.Remove(quorum);
        delete quorum;
    }
    
    // check changes in active or inactive node list
    for (configQuorum = configState->quorums.First();
     configQuorum != NULL;
     configState->quorums.Next(configQuorum))
    {
        for (nit = nodes->First(); nit != NULL; nodes->Next(nit))
        {
            if (*nit != nodeID)
                continue;
            ConfigureQuorum(configQuorum, true);
        }

        for (nit = nodes->First(); nit != NULL; nodes->Next(nit))
        {
            if (*nit != nodeID)
                continue;
            ConfigureQuorum(configQuorum, false);
        }
    }
    
    OnRequestLeaseTimeout();
}

void ShardServer::ConfigureQuorum(ConfigQuorum* configQuorum, bool active)
{
    QuorumData*     quorumData;
    uint64_t        quorumID;
    uint64_t        nodeID;
    
    nodeID = REPLICATION_CONFIG->GetNodeID();
    quorumID = configQuorum->quorumID;
    
    quorumData = LocateQuorum(quorumID);
    if (active && quorumData == NULL)
    {
        quorumData = new QuorumData;
        quorumData->quorumID = quorumID;
        quorumData->proposalID = 0;
        quorumData->context.Init(this, configQuorum, GetQuorumTable(quorumID));
        quorumData->leaseTimeout.SetCallable(MFUNC(ShardServer, OnPrimaryLeaseTimeout));
        quorumData->isPrimary = false;
        quorumData->isActive = true;

        quorums.Append(quorumData);
    }
    else if (quorumData != NULL)
        quorumData->context.UpdateConfig(configQuorum);
    
    UpdateShards(configQuorum->shards);
}

void ShardServer::OnReceiveLease(uint64_t quorumID, uint64_t proposalID)
{
    QuorumData*     quorum;
    
    quorum = LocateQuorum(quorumID);
    if (!quorum)
        return;
    
    if (quorum->proposalID != proposalID)
        return;
    
    quorum->isPrimary = true;
    EventLoop::Add(&quorum->leaseTimeout);
}

void ShardServer::UpdateShards(List<uint64_t>& shards)
{
    uint64_t*           sit;
    ConfigShard*        shard;
    StorageDatabase*    database;
    StorageTable*       table;
    Buffer              name;
    ReadBuffer          firstKey;

    // TODO:
    for (sit = shards.First(); sit != NULL; sit = shards.Next(sit))
    {
        shard = configState->GetShard(*sit);
        assert(shard != NULL);
        
        if (!databases.Get(shard->databaseID, database))
        {
            name.Writef("%U", shard->databaseID);
            name.NullTerminate();

            database = new StorageDatabase;
            database->Open(databaseDir, name.GetBuffer());
            databases.Set(shard->databaseID, database);
        }
        
        if (!tables.Get(shard->tableID, table))
        {
            name.Writef("%U", shard->tableID);
            name.NullTerminate();
            
            table = database->GetTable(name.GetBuffer());
            assert(table != NULL);
            tables.Set(shard->tableID, table);
        }
        
        // TODO: check if already exists
        firstKey.Wrap(shard->firstKey);
        table->CreateShard(*sit, firstKey);
    }
}

StorageTable* ShardServer::LocateTable(uint64_t tableID)
{
    StorageTable*   table;
    
    if (!tables.Get(tableID, table))
        return NULL;
    return table;
}

StorageTable* ShardServer::GetQuorumTable(uint64_t quorumID)
{
    Buffer          qname;
    
    qname.Writef("%U", quorumID);
    qname.NullTerminate();
    return systemDatabase.GetTable(qname.GetBuffer());
}

void ShardServer::OnClientRequestGet(ClientRequest* request)
{
    StorageTable*   table;
    ReadBuffer      key;
    ReadBuffer      value;

    table = LocateTable(request->tableID);
    if (!table)
    {
        request->response.Failed();
        request->OnComplete();
        return;            
    }
    
    key.Wrap(request->key);
    if (!table->Get(key, value))
    {
        request->response.Failed();
        request->OnComplete();
        return;
    }
    
    request->response.Value(value);
    request->OnComplete();
}

