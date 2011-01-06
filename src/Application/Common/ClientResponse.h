#ifndef CLIENTRESPONSE_H
#define CLIENTRESPONSE_H

#include "System/Buffers/ReadBuffer.h"
#include "Application/ConfigState/ConfigState.h"

#define CLIENTRESPONSE_OK               'O'
#define CLIENTRESPONSE_NUMBER           'n'
#define CLIENTRESPONSE_VALUE            'V'
#define CLIENTRESPONSE_CONFIG_STATE     'C'
#define CLIENTRESPONSE_NOSERVICE        'S'
#define CLIENTRESPONSE_FAILED           'F'
#define CLIENTRESPONSE_NORESPONSE       ' '

class ClientRequest; // forward

/*
===============================================================================================

 ClientResponse

===============================================================================================
*/

class ClientResponse
{
public:
    ClientRequest*  request;
    
    /* Variables */
    char            type;
    uint64_t        number;
    uint64_t        commandID;
    ReadBuffer      value;
    Buffer*         valueBuffer;
    ConfigState     configState;

    ClientResponse();
    ~ClientResponse();
    
    void            CopyValue();
    void            Transfer(ClientResponse& other);
            
    /* Responses */
    bool            OK();
    bool            Number(uint64_t number);
    bool            Value(ReadBuffer& value);
    bool            ConfigStateResponse(ConfigState& configState);
    bool            NoService();
    bool            Failed();
    bool            NoResponse();
};

#endif
