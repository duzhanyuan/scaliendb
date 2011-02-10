import random
import sys
import scaliendb
import time
import md5

CONTROLLERS=["localhost:7080"]
#CONTROLLERS=["192.168.137.51:7080"]

def sizeof_fmt(num):
	for x in ['bytes','KB','MB','GB','TB']:
		if num < 1024.0:
			return "%3.1f%s" % (num, x)
		num /= 1024.0

limit = 0
start = 0
if len(sys.argv) > 1:
	limit = int(sys.argv[1])
	if len(sys.argv) > 2:
		start = int(sys.argv[2])
client = scaliendb.Client(CONTROLLERS)
#client._set_trace(True)

if False:
	quorum_id = client.create_quorum(["100"])
	database_id = client.create_database("testdb")
	client.create_table(database_id, quorum_id, "testtable")

if False:
	database_id = client.get_database_id("testdb")
	table_id = client.get_table_id(database_id, "testtable")
	client.truncate_table(table_id)
                        
client.use_database("testdb")
client.use_table("testtable")
sent = 0
value = "%100s" % " "
i = start
batch = 10000
while limit == 0 or i < limit:
	starttime = time.time()
	client.begin()
	for x in xrange(batch):
		#client.set(str(random.randint(1, 1000000000)), value)
		client.set(md5.new(str(x + i)).hexdigest(), value)
		sent += len(value)
	ret = client.submit()
	if ret != 0:
		print("Submit failed!")
		break
	endtime = time.time()
	i += batch
	fmt = "%H:%M:%S"
	endtimestamp = time.strftime(fmt, time.gmtime())
	print("%s: Sent bytes: %s, num: %i, rps = %.0f" % (endtimestamp, sizeof_fmt(sent), i, (batch/((endtime - starttime) * 1000.0) * 1000.0)))