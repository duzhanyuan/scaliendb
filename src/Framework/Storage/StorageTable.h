#ifndef STORAGETABLE_H
#define STORAGETABLE_H

#include "System/Containers/InTreeMap.h"
#include "System/IO/FD.h"
#include "StorageShard.h"
#include "StorageShardIndex.h"

/*
===============================================================================

 StorageTable

===============================================================================
*/

class StorageTable
{
public:
	typedef InTreeMap<StorageShardIndex> StorageIndexMap;

	const char*			GetName();
	uint64_t			GetSize();
	
	void				Open(const char* path, const char* name);
	void				Commit(bool recovery = true, bool flush = true);
	void				Close();
		
	bool				Get(ReadBuffer& key, ReadBuffer& value);
	bool				Set(ReadBuffer& key, ReadBuffer& value, bool copy = true);
	void				Delete(ReadBuffer& key);

	bool				CreateShard(uint64_t shardID, ReadBuffer& startKey, ReadBuffer& endKey);
	bool				SplitShard(uint64_t oldShardID, uint64_t newShardID, ReadBuffer& startKey);

	StorageTable*		next;
	StorageTable*		prev;

private:
	StorageShardIndex*	Locate(ReadBuffer& key);
	void				PerformRecovery(uint64_t length);
	void				PerformRecoveryCopy();
	void				PerformRecoveryMove();
	uint64_t			PerformRecoveryCreateShard();
	void				ReadTOC(uint64_t length);
	void				WriteTOC();
	
	void				WriteRecoveryDone();
	void				WriteRecoveryCreateShard(uint64_t newShardID);
	void				WriteRecoveryCopy(uint64_t oldShardID, uint32_t fileIndex);
	void				WriteRecoveryMove(Buffer& src, Buffer& dst);
	void				CommitPhase1();
	void				CommitPhase2();
	void				CommitPhase3();
	void				CommitPhase4();

	FD					tocFD;
	FD					recoveryFD;
	Buffer				tocFilepath;
	Buffer				recoveryFilepath;
	Buffer				name;
	Buffer				path;
	Buffer				buffer;
	StorageIndexMap		shards;
	
	friend class StorageDatabase;
};

#endif
