#include "System/Config.h"
#include "Framework/Database/Database.h"
#include "Framework/Replication/ReplicationManager.h"
#include "Application/Controller/ControllerConfigContext.h"
#include "Application/Controller/Controller.h"
#include "Application/HTTP/HttpServer.h"

int main(int argc, char** argv)
{
	DatabaseConfig				dbConfig;
	Database					db;
	ControllerConfigContext		ctx;
	Controller					controller;
	SingleQuorum				quorum;
	QuorumDatabase				qdb;
	QuorumTransport				qtransport;
	Buffer						prefix;
	HttpServer					httpServer;
	
	configFile.Init(argv[1]);

	Log_SetTarget(LOG_TARGET_STDOUT);
	Log_SetTrace(true);
	Log_SetTimestamping(true);
		
	IOProcessor::Init(1024);

	dbConfig.dir = configFile.GetValue("database.dir", DATABASE_CONFIG_DIR);
	db.Init(dbConfig);
	
	RMAN->SetNodeID(configFile.GetIntValue("nodeID", 0));
	unsigned numNodes = configFile.GetListNum("controllers");
	for (unsigned i = 0; i < numNodes; i++)
	{
		const char* s = configFile.GetListValue("controllers", i, NULL);
		Endpoint endpoint;
		endpoint.Set(s);
		quorum.AddNode(i);

		// TODO: i will not be a nodeID once we start using UIDs
		RMAN->GetTransport()->AddEndpoint(i, endpoint);
	}
	
	RMAN->GetTransport()->Start();

	qdb.Init(db.GetTable("keyspace"));
	prefix.Write("0");
	qtransport.SetPriority(true);
	qtransport.SetPrefix(prefix);
	qtransport.SetQuorum(&quorum);
	
	ctx.SetContextID(0);
	ctx.SetQuorum(quorum);
	ctx.SetDatabase(qdb);
	ctx.SetTransport(qtransport);
	ctx.Start();

	controller.SetContext(&ctx);

	httpServer.Init(configFile.GetIntValue("http.port", 8080));
	httpServer.RegisterHandler(&controller);
	
	RMAN->AddContext(&ctx);
	
	EventLoop::Init();
	EventLoop::Run();
	EventLoop::Shutdown();
}
