#!/bin/bash
write_ratio=(10 30 50 70 90)
log_name_base="${1}"
log_name_base="${log_name_base:=output_numa}"

for ratio in ${write_ratio[@]}
do
    log_file="${log_name_base}_${ratio}.log"
    g++ -Ofast -g -ggdb -DWRITE_RATIO=${ratio} -o NUMA_CX_Skiplist main.cpp -lpthread -lnuma
    for i in {1..10}
    do
        ./NUMA_CX_Skiplist
    done | tee "${log_file}"
done
