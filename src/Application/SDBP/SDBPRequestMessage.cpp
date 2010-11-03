#include "SDBPRequestMessage.h"

bool SDBPRequestMessage::Read(ReadBuffer& buffer)
{
    int         read;
    unsigned    i, numNodes;
    uint64_t    nodeID;
        
    if (buffer.GetLength() < 1)
        return false;
    
    switch (buffer.GetCharAt(0))
    {
        /* Master query */
        case CLIENTREQUEST_GET_MASTER:
            read = buffer.Readf("%c:%U",
             &request->type, &request->commandID);
            break;

        /* Get config state: databases, tables, shards, quora */
        case CLIENTREQUEST_GET_CONFIG_STATE:
            read = buffer.Readf("%c:%U",
             &request->type, &request->commandID);
            break;

        /* Quorum management */
        case CLIENTREQUEST_CREATE_QUORUM:
            read = buffer.Readf("%c:%U:%u",
             &request->type, &request->commandID, &numNodes);
            if (read < 0 || read == (signed)buffer.GetLength())
                return false;
            buffer.Advance(read);
            for (i = 0; i < numNodes; i++)
            {
                read = buffer.Readf(":%U", &nodeID);
                if (read < 0)
                    return false;
                buffer.Advance(read);
                request->nodes.Append(nodeID);
            }
            if (buffer.GetLength() == 0)
                return true;
            else
                return false;
            break;

        /* Database management */
        case CLIENTREQUEST_CREATE_DATABASE:
            read = buffer.Readf("%c:%U:%#B",
             &request->type, &request->commandID, &request->name);
            break;
        case CLIENTREQUEST_RENAME_DATABASE:
            read = buffer.Readf("%c:%U:%U:%#B",
             &request->type, &request->commandID, &request->databaseID,
             &request->name);
            break;
        case CLIENTREQUEST_DELETE_DATABASE:
            read = buffer.Readf("%c:%U:%U",
             &request->type, &request->commandID, &request->databaseID);
            break;
            
        /* Table management */
        case CLIENTREQUEST_CREATE_TABLE:
            read = buffer.Readf("%c:%U:%U:%U:%#B",
             &request->type, &request->commandID, &request->databaseID,
             &request->quorumID, &request->name);
            break;
        case CLIENTREQUEST_RENAME_TABLE:
            read = buffer.Readf("%c:%U:%U:%U:%#B",
             &request->type, &request->commandID, &request->databaseID,
             &request->tableID, &request->name);
            break;
        case CLIENTREQUEST_DELETE_TABLE:
            read = buffer.Readf("%c:%U:%U:%U",
             &request->type, &request->commandID, &request->databaseID,
             &request->tableID);
            break;
            
        /* Data operations */
        case CLIENTREQUEST_GET:
            read = buffer.Readf("%c:%U:%U:%U:%#B",
             &request->type, &request->commandID, &request->databaseID,
             &request->tableID, &request->key);
            break;
        case CLIENTREQUEST_SET:
        case CLIENTREQUEST_SET_IF_NOT_EXISTS:
            read = buffer.Readf("%c:%U:%U:%U:%#B:%#B",
             &request->type, &request->commandID, &request->databaseID,
             &request->tableID, &request->key, &request->value);
            break;
        case CLIENTREQUEST_TEST_AND_SET:
            read = buffer.Readf("%c:%U:%U:%U:%#B:%#B:%#B",
             &request->type, &request->commandID, &request->databaseID,
             &request->tableID, &request->key, &request->test, &request->value);
            break;
        case CLIENTREQUEST_ADD:
            read = buffer.Readf("%c:%U:%U:%U:%#B:%I",
             &request->type, &request->commandID, &request->databaseID,
             &request->tableID, &request->key, &request->number);
            break;
        case CLIENTREQUEST_DELETE:
        case CLIENTREQUEST_REMOVE:
            read = buffer.Readf("%c:%U:%U:%U:%#B",
             &request->type, &request->commandID, &request->databaseID,
             &request->tableID, &request->key);
            break;
        default:
            return false;
    }
    
    return (read == (signed)buffer.GetLength() ? true : false);
}

bool SDBPRequestMessage::Write(Buffer& buffer)
{
    uint64_t*   it;
    
    switch (request->type)
    {
        /* Master query */
        case CLIENTREQUEST_GET_MASTER:
            buffer.Appendf("%c:%U",
             request->type, request->commandID);
            return true;

        /* Get config state: databases, tables, shards, quora */
        case CLIENTREQUEST_GET_CONFIG_STATE:
            buffer.Appendf("%c:%U",
             request->type, request->commandID);
            return true;

        /* Quorum management */
        case CLIENTREQUEST_CREATE_QUORUM:
            buffer.Appendf("%c:%U:%u",
             request->type, request->commandID, request->nodes.GetLength());
            for (it = request->nodes.First(); it != NULL; it = request->nodes.Next(it))
                buffer.Appendf(":%U", *it);
            return true;
            

        /* Database management */
        case CLIENTREQUEST_CREATE_DATABASE:
            buffer.Appendf("%c:%U:%#B",
             request->type, request->commandID, &request->name);
            return true;
        case CLIENTREQUEST_RENAME_DATABASE:
            buffer.Appendf("%c:%U:%U:%#B",
             request->type, request->commandID, request->databaseID,
             &request->name);
            return true;
        case CLIENTREQUEST_DELETE_DATABASE:
            buffer.Appendf("%c:%U:%U",
             request->type, request->commandID, request->databaseID);
            return true;

        /* Table management */
        case CLIENTREQUEST_CREATE_TABLE:
            buffer.Appendf("%c:%U:%U:%U:%#B",
             request->type, request->commandID, request->databaseID,
             request->quorumID, &request->name);
            return true;
        case CLIENTREQUEST_RENAME_TABLE:
            buffer.Appendf("%c:%U:%U:%U:%#B",
             request->type, request->commandID, request->databaseID,
             request->tableID, &request->name);
            return true;
        case CLIENTREQUEST_DELETE_TABLE:
            buffer.Appendf("%c:%U:%U:%U",
             request->type, request->commandID, request->databaseID,
             request->tableID);
            return true;

        /* Data operations */
        case CLIENTREQUEST_GET:
            buffer.Appendf("%c:%U:%U:%U:%#B",
             request->type, request->commandID, request->databaseID,
             request->tableID, &request->key);
            return true;
        case CLIENTREQUEST_SET:
        case CLIENTREQUEST_SET_IF_NOT_EXISTS:
            buffer.Appendf("%c:%U:%U:%U:%#B:%#B",
             request->type, request->commandID, request->databaseID,
             request->tableID, &request->key, &request->value);
            return true;
        case CLIENTREQUEST_TEST_AND_SET:
            buffer.Appendf("%c:%U:%U:%U:%#B:%#B:%#B",
             request->type, request->commandID, request->databaseID,
             request->tableID, &request->key, &request->test, &request->value);
            return true;
        case CLIENTREQUEST_ADD:
            buffer.Appendf("%c:%U:%U:%U:%#B:%I",
             request->type, request->commandID, request->databaseID,
             request->tableID, &request->key, request->number);
            return true;
        case CLIENTREQUEST_DELETE:
        case CLIENTREQUEST_REMOVE:
            buffer.Appendf("%c:%U:%U:%U:%#B",
             request->type, request->commandID, request->databaseID,
             request->tableID, &request->key);
            return true;

        default:
            return false;
    }
}
