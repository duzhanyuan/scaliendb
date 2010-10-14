#ifndef SDBPRESULT_H
#define SDBPRESULT_H

#include "System/Containers/InTreeMap.h"
#include "Application/Common/ClientResponse.h"
#include "SDBPClientRequest.h"

namespace SDBPClient
{

/*
===============================================================================================

 SDBPClient::Result

===============================================================================================
*/

class Result
{
public:
    ~Result();
    
    void            Close();
    
    void            Begin();
    void            Next();
    bool            IsEnd();
    
    void            AppendRequest(Request* req);
    bool            AppendRequestResponse(ClientResponse* resp);

    int             CommandStatus();
    int             TransportStatus();
    int             TimeoutStatus();

    int             GetKey(ReadBuffer& key);
    int             GetValue(ReadBuffer& value);
    int             GetNumber(uint64_t& number);
    
    int             GetDatabaseID(uint64_t& databaseID);
    int             GetTableID(uint64_t& tableID);

    unsigned        GetRequestCount();

private:
    typedef InTreeMap<Request> RequestMap;
    friend class Client;
    
    RequestMap      requests;
    int             transportStatus;
    int             timeoutStatus;
    int             connectivityStatus;
    unsigned        numCompleted;
    Request*        requestCursor;
};

};  // namespace

#endif
