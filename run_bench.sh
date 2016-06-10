#!/bin/bash

USE_DMA="0 1"
USE_AVX="0 1"
PAGE_ORDERS=`seq 0 10`
ITERATION=16


for I in `seq 1 5`
do
	for ORDER in ${PAGE_ORDERS}
	do
		for AVX in ${USE_AVX}
		do
			for DMA in ${USE_DMA}
			do
				if [[ "x${DMA}" == "x1" && "x${AVX}" == "x1"   ]]; then
					continue
				fi
				echo DMA: ${DMA}, AVX: ${AVX}, ORDER: ${ORDER}
				if [[ "x${I}" == "x1" ]]; then
				sudo taskset 0x2 insmod pref-test.ko use_dma=${DMA} use_avx=${AVX} page_order=${ORDER} iterations=${ITERATION} && sudo rmmod pref_test && sudo dmesg -c | grep "Page copy time" > ./stats/dma_${DMA}_avx_${AVX}_page_order_${ORDER}
				else
				sudo taskset 0x2 insmod pref-test.ko use_dma=${DMA} use_avx=${AVX} page_order=${ORDER} iterations=${ITERATION} && sudo rmmod pref_test && sudo dmesg -c | grep "Page copy time" >> ./stats/dma_${DMA}_avx_${AVX}_page_order_${ORDER}

				fi
			done
		done
	done
done
