#!/bin/bash

USE_DMA="1"
USE_AVX="0"
PAGE_ORDERS=`seq 0 10`
ITERATION=16
NUM_DMA="1 2 4 8 16"

sudo dmesg -c >/dev/null

for I in `seq 1 2`
do
	for ORDER in ${PAGE_ORDERS}
	do
		#for AVX in ${USE_AVX}
		#do
			for DMA in ${USE_DMA}
			do
				if [[ "x${DMA}" == "x1" && "x${AVX}" == "x1"   ]]; then
					continue
				fi
				for N_DMA in ${NUM_DMA}
				do

					echo DMA: ${DMA}, AVX: ${AVX}, ORDER: ${ORDER}, NUM_DMA: ${N_DMA}
					if [[ "x${I}" == "x1" ]]; then
					RESULT=`sudo taskset 0x2 insmod pref-test.ko use_dma=${DMA} use_multi_dma=${N_DMA} page_order=${ORDER} iterations=${ITERATION} && sudo rmmod pref_test && sudo dmesg -c | grep "Page copy time"`
					echo -n "${RESULT}, " >  ./stats/dma_${DMA}_page_order_${ORDER}_num_dma_${N_DMA}
					RESULT=`echo ${RESULT} | sed "s/\[.\+\]/time/g"`
					TIME=`echo ${RESULT} | cut -d" " -f 7`
					BANDWIDTH=`echo "scale=5; 4*2^${ORDER}*${ITERATION}*10^6/${TIME}/1024/1024" | bc`
					echo "${BANDWIDTH} GB/s" >>./stats/dma_${DMA}_page_order_${ORDER}_num_dma_${N_DMA}
					else
					RESULT=`sudo taskset 0x2 insmod pref-test.ko use_dma=${DMA} use_multi_dma=${N_DMA} page_order=${ORDER} iterations=${ITERATION} && sudo rmmod pref_test && sudo dmesg -c | grep "Page copy time"`
					echo -n  "${RESULT}, " >>  ./stats/dma_${DMA}_page_order_${ORDER}_num_dma_${N_DMA}
					RESULT=`echo ${RESULT} | sed "s/\[.\+\]/time/g"`
					TIME=`echo ${RESULT} | cut -d" " -f 7`
					BANDWIDTH=`echo "scale=5; 4*2^${ORDER}*${ITERATION}*10^6/${TIME}/1024/1024" | bc`
					echo "${BANDWIDTH} GB/s" >>./stats/dma_${DMA}_page_order_${ORDER}_num_dma_${N_DMA}
					fi

				done
			done
		#done
	done
done
