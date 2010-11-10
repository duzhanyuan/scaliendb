#ifndef HTTPSHARDSERVERSESSION_H
#define HTTPSHARDSERVERSESSION_H

#include "Application/Common/ClientSession.h"
#include "Application/HTTP/HTTPSession.h"
#include "Application/HTTP/UrlParam.h"

class ShardServer;      // forward
class ClientRequest;    // forward

/*
===============================================================================================

 ShardHTTPClientSession

===============================================================================================
*/

class ShardHTTPClientSession : public ClientSession
{
public:
    void                SetShardServer(ShardServer* shardServer);
    void                SetConnection(HTTPConnection* conn);

    bool                HandleRequest(HTTPRequest& request);

    // ========================================================================================
    // ClientSession interface
    //
    virtual void        OnComplete(ClientRequest* request, bool last);
    virtual bool        IsActive();
    // ========================================================================================

private:
    void                PrintStatus();
    bool                ProcessCommand(ReadBuffer& cmd);
    ClientRequest*      ProcessShardServerCommand(ReadBuffer& cmd);
    ClientRequest*      ProcessGetPrimary();
    ClientRequest*      ProcessGet();
    ClientRequest*      ProcessSet();
    ClientRequest*      ProcessSetIfNotExists();
    ClientRequest*      ProcessTestAndSet();
    ClientRequest*      ProcessAdd();
    ClientRequest*      ProcessDelete();
    ClientRequest*      ProcessRemove();
    void                OnConnectionClose();

    ShardServer*        shardServer;
    HTTPSession         session;
    UrlParam            params;
};

#endif