#!/bin/bash

PAGE_ORDERS=`seq 0 10`

NUM_MT="1 2 4 8 16"

sudo dmesg -c >/dev/null

for I in `seq 1 5`
do
	for N_MT in ${NUM_MT}
	do
		for ORDER in ${PAGE_ORDERS}
		do

			echo ORDER: ${ORDER}, NUM_MT: ${N_MT}
			if [[ "x${I}" == "x1" ]]; then
			RESULT=`sudo taskset 0x1 insmod pref-test.ko use_dma=${DMA} use_multi_dma=${N_DMA} page_order=${ORDER} iterations=${ITERATION} && sudo rmmod pref_test && sudo dmesg -c | grep "Page order:"`
			echo -n "${RESULT}, " >  ./stats/page_order_${ORDER}_num_mt_${N_MT}
			RESULT=`echo ${RESULT} | sed "s/\[.\+\]/time/g"`
			TIME=`echo ${RESULT} | cut -d" " -f 10`
			BANDWIDTH=`echo "scale=5; 4*2^${ORDER}*${N_MT}*10^6/${TIME}/1024/1024" | bc`
			#BANDWIDTH=`echo "scale=5; 4*2^${ORDER}*10^6/${TIME}/1024/1024" | bc`
			echo "${BANDWIDTH} GB/s" >>./stats/page_order_${ORDER}_num_mt_${N_MT}
			else
			RESULT=`sudo taskset 0x1 insmod pref-test.ko use_dma=${DMA} use_multi_dma=${N_DMA} page_order=${ORDER} iterations=${ITERATION} && sudo rmmod pref_test && sudo dmesg -c | grep "Page order:"`
			echo -n "${RESULT}, " >>  ./stats/page_order_${ORDER}_num_mt_${N_MT}
			RESULT=`echo ${RESULT} | sed "s/\[.\+\]/time/g"`
			TIME=`echo ${RESULT} | cut -d" " -f 10`
			BANDWIDTH=`echo "scale=5; 4*2^${ORDER}*${N_MT}*10^6/${TIME}/1024/1024" | bc`
			#BANDWIDTH=`echo "scale=5; 4*2^${ORDER}*10^6/${TIME}/1024/1024" | bc`
			echo "${BANDWIDTH} GB/s" >>./stats/page_order_${ORDER}_num_mt_${N_MT}
			fi

			sleep 1

		done
	done
done
