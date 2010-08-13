#include "MessageTransport.h"

void MessageTransport::Init(uint64_t nodeID_, Endpoint& endpoint_)
{
	nodeID = nodeID_;
	endpoint = endpoint_;
	server.Init(endpoint.GetPort());
	server.SetTransport(this);
}

void MessageTransport::AddEndpoint(uint64_t nodeID, Endpoint endpoint)
{
	MessageConnection* conn;

	if (nodeID < this->nodeID)
		return;
	
	conn = GetConnection(nodeID);
	if (conn != NULL)
		return;

	if (nodeID == this->nodeID)
		Log_Trace("connecting to self");

	conn = new MessageConnection;
	conn->SetTransport(this);
	conn->SetNodeID(nodeID);
	conn->SetEndpoint(endpoint);
	conn->Connect();	
}

void MessageTransport::SendMessage(uint64_t nodeID, Buffer& prefix, Message& msg)
{
	MessageConnection*	conn;
	
	conn = GetConnection(nodeID);
	
	if (!conn)
	{
		Log_Trace("no connection to nodeID %" PRIu64, nodeID);
		return;
	}
	
	if (conn->GetProgress() != MessageConnection::READY)
	{
		Log_Trace("connection to %" PRIu64 " has progress: %d", nodeID, conn->GetProgress());
		return;
	}
	
	conn->Write(prefix, msg);
}

void MessageTransport::SendPriorityMessage(uint64_t nodeID, Buffer& prefix, Message& msg)
{
	MessageConnection*	conn;
	
	conn = GetConnection(nodeID);
	
	if (!conn)
	{
		Log_Trace("no connection to nodeID %" PRIu64, nodeID);
		return;
	}
	
	if (conn->GetProgress() != MessageConnection::READY)
	{
		Log_Trace("connection to %" PRIu64 " has progress: %d", nodeID, conn->GetProgress());
		return;
	}
	
	conn->WritePriority(prefix, msg);
}

void MessageTransport::AddConnection(MessageConnection* conn)
{
	conns.Append(conn);
}

MessageConnection* MessageTransport::GetConnection(uint64_t nodeID)
{
	MessageConnection* it;
	
	for (it = conns.Head(); it != NULL; it = conns.Next(it))
	{
		if (it->GetNodeID() == nodeID)
			return it;
	}
	
	return NULL;
}

void MessageTransport::DeleteConnection(MessageConnection* conn)
{
	conn->Close();

	if (conn->next != conn)
		conns.Remove(conn);

	delete conn; // TODO: what happens when control returns to OnRead()
}

uint64_t MessageTransport::GetNodeID()
{
	return nodeID;
}

Endpoint& MessageTransport::GetEndpoint()
{
	return endpoint;
}
