#!/bin/bash

#PAGE_ORDERS=`seq 0 10`
#PAGE_ORDERS="0 4 5 8 9 12 14 20"
#PAGE_ORDERS="20"
#PAGE_ORDERS="0 4 5 9 13 14"
PAGE_ORDERS="4"

NUM_MT="8" #" 16 32 64"
#NUM_MT="32 64"

STATS_FOLDER="stats_1gb_mov"
sudo dmesg -c >/dev/null

PAGESIZE=64

CYCLE_TO_SEC=512000000

for I in `seq 1 3`
do
	for N_MT in ${NUM_MT}
	do
		for ORDER in ${PAGE_ORDERS}
		do

			echo ORDER: ${ORDER}, NUM_MT: ${N_MT}

			if [[ "${ORDER}x" == "20x" ]]; then
				C_ORDER=0
				C_PAGESIZE=4
			else
				C_ORDER=${ORDER}
				C_PAGESIZE=${PAGESIZE}
			fi
			if [[ "x${I}" == "x1" ]]; then
			RESULT=`sudo taskset 0x10000000000000000 insmod pref-test.ko page_order=${ORDER} node=0 nthreads=${N_MT} && sudo rmmod pref_test && sudo dmesg -c | grep "Page order:"`
			echo -n "${RESULT}, " >  ./${STATS_FOLDER}/page_order_${ORDER}_num_mt_${N_MT}
			RESULT=`echo ${RESULT} | sed "s/\[.\+\]/time/g"`
			TIME=`echo ${RESULT} | cut -d" " -f 8`
			#BANDWIDTH=`echo "scale=5; 4*2^${ORDER}*${N_MT}*10^6/${TIME}/1024/1024" | bc`
			BANDWIDTH=`echo "scale=5; ${C_PAGESIZE}*2^${C_ORDER}*${CYCLE_TO_SEC}/${TIME}/1024/1024" | bc`
			echo "${BANDWIDTH} GB/s" >>./${STATS_FOLDER}/page_order_${ORDER}_num_mt_${N_MT}
			else
			RESULT=`sudo taskset 0x10000000000000000 insmod pref-test.ko page_order=${ORDER} node=0 nthreads=${N_MT} && sudo rmmod pref_test && sudo dmesg -c | grep "Page order:"`
			echo -n "${RESULT}, " >>  ./${STATS_FOLDER}/page_order_${ORDER}_num_mt_${N_MT}
			RESULT=`echo ${RESULT} | sed "s/\[.\+\]/time/g"`
			TIME=`echo ${RESULT} | cut -d" " -f 8`
			#BANDWIDTH=`echo "scale=5; 4*2^${ORDER}*${N_MT}*10^6/${TIME}/1024/1024" | bc`
			BANDWIDTH=`echo "scale=5; ${C_PAGESIZE}*2^${C_ORDER}*${CYCLE_TO_SEC}/${TIME}/1024/1024" | bc`
			echo "${BANDWIDTH} GB/s" >>./${STATS_FOLDER}/page_order_${ORDER}_num_mt_${N_MT}
			fi

			echo "Done"
			sync
			sleep 5

		done
	done
done
