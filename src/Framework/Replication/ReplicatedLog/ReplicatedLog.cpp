#include "ReplicatedLog.h"
#include "ReplicationManager.h"
#include "System/Events/EventLoop.h"

static Buffer enableMultiPaxos;

void ReplicatedLog::Init(QuorumContext* context_)
{
	Log_Trace();
	
	context = context_;
	
	proposer.Init(context);
	acceptor.Init(context);

	paxosID = 0;

	lastRequestChosenTime = 0;
	lastRequestChosenPaxosID = 0;
	
//	lastStarted = EventLoop::Now();
//	lastLength = 0;
//	lastTook = 0;
//	thruput = 0;
		
//	logCache.Init(acceptor.paxosID); // TODO
	enableMultiPaxos.Write("EnableMultiPaxos");
}

void ReplicatedLog::TryAppendNextValue()
{
	Buffer* buffer;
	
	Log_Trace();
	
	if (!context->IsLeader() || proposer.IsActive() || !proposer.state.multi)
		return;
	
	buffer = context->GetNextValue();
	if (buffer == NULL)
		return;
	
	Append(*buffer);
}

void ReplicatedLog::Append(const Buffer& value)
{
	Log_Trace();
		
	if (proposer.IsActive())
		return;
	
	proposer.Propose(value);
}

void ReplicatedLog::SetPaxosID(uint64_t paxosID_)
{
	paxosID = paxosID_;
}

uint64_t ReplicatedLog::GetPaxosID() const
{
	return paxosID;
}

void ReplicatedLog::OnMessage(const PaxosMessage& imsg)
{
	Log_Trace();
	
	if (imsg.type == PAXOS_PREPARE_REQUEST)
		OnPrepareRequest(imsg);
	else if (imsg.IsPrepareResponse())
		OnPrepareResponse(imsg);
	else if (imsg.type == PAXOS_PROPOSE_REQUEST)
		OnProposeRequest(imsg);
	else if (imsg.IsProposeResponse())
		OnProposeResponse(imsg);
	else if (imsg.IsLearn())
		OnLearnChosen(imsg);
	else if (imsg.type == PAXOS_REQUEST_CHOSEN)
		OnRequestChosen(imsg);
//	else if (imsg.type == PAXOS_START_CATCHUP)
//		OnStartCatchup();
	else
		ASSERT_FAIL();
}

void ReplicatedLog::OnPrepareRequest(const PaxosMessage& imsg)
{
	Log_Trace();
		
	if (imsg.paxosID == paxosID)
		acceptor.OnPrepareRequest(imsg);

	OnRequest(imsg);
}

void ReplicatedLog::OnPrepareResponse(const PaxosMessage& imsg)
{
	Log_Trace();
	
	if (imsg.paxosID == paxosID)
		proposer.OnPrepareResponse(imsg);
}

void ReplicatedLog::OnProposeRequest(const PaxosMessage& imsg)
{
	Log_Trace();
	
	if (imsg.paxosID == paxosID)
		acceptor.OnProposeRequest(imsg);
	
	OnRequest(imsg);
}

void ReplicatedLog::OnProposeResponse(const PaxosMessage& imsg)
{
	Log_Trace();

	if (imsg.paxosID == paxosID)
		proposer.OnProposeResponse(imsg);
}

void ReplicatedLog::OnLearnChosen(const PaxosMessage& imsg)
{
	uint64_t		runID;
	bool			commit;
	Buffer*			value;

	Log_Trace();

	if (imsg.paxosID > paxosID)
	{
		RequestChosen(imsg.nodeID); //	I am lagging and need to catch-up
		return;
	}
	else if (imsg.paxosID < paxosID)
		return;
	
	if (imsg.type == PAXOS_LEARN_VALUE)
	{
		runID = imsg.runID;
		value = imsg.value;
	}
	else if (imsg.type == PAXOS_LEARN_PROPOSAL && acceptor.state.accepted &&
	 acceptor.state.acceptedProposalID == imsg.proposalID)
	 {
		runID = acceptor.state.acceptedRunID;
		value = &acceptor.state.acceptedValue;
	}
	else
	{
		RequestChosen(imsg.nodeID);
		return;
	}
	
	commit = (paxosID == (context->GetHighestPaxosID() - 1));
	logCache.Set(paxosID, *value, commit);

	NewPaxosRound(); // increments paxosID, clears proposer, acceptor
	
	if (context->GetHighestPaxosID() >= paxosID)
		RequestChosen(imsg.nodeID);
	
	if (imsg.nodeID == RMAN->GetNodeID() && runID == RMAN->GetRunID() && context->IsLeader())
	{
		proposer.state.multi = true;
		Log_Trace("Multi paxos enabled");
	}
	else
	{
		proposer.state.multi = false;
		Log_Trace("Multi paxos disabled");
	}
	
//	if (!Buffer::Cmp(*imsg.value, enableMultiPaxos))
//		replicatedDB->OnAppend(GetTransaction(), paxosID - 1,  value, proposer.state.multi);
	TryAppendNextValue();
}

void ReplicatedLog::OnRequestChosen(const PaxosMessage& imsg)
{
	Buffer*			value;
	PaxosMessage	omsg;
	
	Log_Trace();

	if (imsg.paxosID >= GetPaxosID())
		return;
	
	// the node is lagging and needs to catch-up
	value = logCache.Get(imsg.paxosID);
	if (value != NULL)
	{
		Log_Trace("Sending paxosID %d to node %d", imsg.paxosID, imsg.nodeID);
		omsg.LearnValue(imsg.paxosID, RMAN->GetNodeID(), 0, value);
		context->GetTransport()->SendMessage(imsg.nodeID, omsg);
	}
//	else // TODO
//	{
//		Log_Trace("Node requested a paxosID I no longer have");
//		SendStartCatchup(pmsg.nodeID, pmsg.paxosID);
//	}
}

//void ReplicatedLog::OnStartCatchup()
//{
//	Log_Trace();
//
//	if (pmsg.paxosID == GetPaxosID() &&
//		replicatedDB != NULL &&
//		!replicatedDB->IsCatchingUp() &&
//		masterLease.IsLeaseKnown())
//	{
//		replicatedDB->OnDoCatchup(masterLease.GetLeaseOwner());
//	}
//}

void ReplicatedLog::OnRequest(const PaxosMessage& imsg)
{
	Log_Trace();
	
	Buffer*			value;
	PaxosMessage	omsg;

	if (imsg.paxosID < GetPaxosID())
	{
		// the node is lagging and needs to catch-up
		value = logCache.Get(imsg.paxosID);
		if (value == NULL)
			return;
		omsg.LearnValue(imsg.paxosID, RMAN->GetNodeID(), 0, value);
		context->GetTransport()->SendMessage(imsg.nodeID, omsg);
	}
	else // paxosID < msg.paxosID
	{
		//	I am lagging and need to catch-up
		RequestChosen(imsg.nodeID);
	}
}

void ReplicatedLog::NewPaxosRound()
{
//	uint64_t now;
//	now = EventLoop::Now();
//	lastTook = ABS(now - lastStarted);
//	lastLength = learner.state.value.length;
//	thruput = (uint64_t)(lastLength / (lastTook / 1000.0));
//	lastStarted = now;
	
	EventLoop::Remove(&(proposer.prepareTimeout));
	EventLoop::Remove(&(proposer.proposeTimeout));

	proposer.state.Init();

	paxosID++;
	acceptor.state.Init();
}

void ReplicatedLog::OnLearnLease()
{
	if (context->IsLeader() && !proposer.IsActive() && !proposer.state.multi)
	{
		Log_Trace("Appending EnableMultiPaxos");
//		Append(enableMultiPaxos); // TODO: commented out for debugging
	}
}

void ReplicatedLog::OnLeaseTimeout()
{
	proposer.state.multi = false;
}

bool ReplicatedLog::IsAppending()
{
	return context->IsLeader() && proposer.state.numProposals > 0;
}

//Transaction* ReplicatedLog::GetTransaction()
//{
//	return &acceptor.transaction;
//}

//bool ReplicatedLog::IsMultiRound()
//{
//	return proposer.state.multi;
//}

void ReplicatedLog::RegisterPaxosID(uint64_t paxosID, uint64_t nodeID)
{
	if (paxosID > GetPaxosID())
	{
		//	I am lagging and need to catch-up
		RequestChosen(nodeID);
	}
}

//uint64_t ReplicatedLog::GetLastRound_Length()
//{
//	return lastLength;
//}
//
//uint64_t ReplicatedLog::GetLastRound_Time()
//{
//	return lastTook;
//}
//
//uint64_t ReplicatedLog::GetLastRound_Thruput()
//{
//	return thruput;
//}

void ReplicatedLog::RequestChosen(uint64_t nodeID)
{
	PaxosMessage omsg;
	
	Log_Trace();
	
	if (lastRequestChosenPaxosID == GetPaxosID() &&
	 EventLoop::Now() - lastRequestChosenTime < REQUEST_CHOSEN_TIMEOUT)
		return;
	
	lastRequestChosenPaxosID = GetPaxosID();
	lastRequestChosenTime = EventLoop::Now();
	
	omsg.RequestChosen(GetPaxosID(), RMAN->GetNodeID());
	
	context->GetTransport()->SendMessage(nodeID, omsg);
}
