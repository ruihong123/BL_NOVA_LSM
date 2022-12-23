#!/bin/bash

basedir="/proj/bg-PG0/haoyu"
numServers=$1
prefix="h"

for ((i=0;i<numServers;i++)); do
	echo "*******************************************"
	echo "*******************************************"
   	echo "******************* node-$i ********************"
   	echo "*******************************************"
   	echo "*******************************************"
#
 	#ssh -oStrictHostKeyChecking=no node-$i "sudo apt-get update && sudo apt install htop -y && cd /users/Ruihong && git clone https://github.com/ruihong123/BL_NOVA_LSM && cd BL_NOVA_LSM && bash scripts/bootstrap/env/install-deps.sh && mkdir build && cd build &&  cmake -DCMAKE_BUILD_TYPE=Release .. && make db_bench nova_server_main -j"
 	ssh -oStrictHostKeyChecking=no node-$i "cd /users/Ruihong && cd BL_NOVA_LSM && cd build && git pull &&  cmake -DCMAKE_BUILD_TYPE=Release .. && make db_bench nova_server_main -j"

#    ssh -oStrictHostKeyChecking=no node-$i "sudo apt-get --yes install screen && ulimit -n 16384 "
#    ssh -oStrictHostKeyChecking=no node-$i "ulimit -n 16384"
    #ssh -n -f -oStrictHostKeyChecking=no node-$i screen -L -S env1 -dm "$basedir/scripts/env/setup-all.sh"
done

sleep 10

sleepcount="0"

for ((i=0;i<numServers;i++)); 
do
	while ssh -oStrictHostKeyChecking=no  node-$i "screen -list | grep -q env1"
	do 
		((sleepcount++))
		sleep 10
		echo "waiting for node-$i "
	done

done

echo "init env took $((sleepcount/6)) minutes"
