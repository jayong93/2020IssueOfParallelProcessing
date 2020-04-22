#!/bin/bash
proportions=(30 50 70 90)

project_dir=${1:?"give a project's root dir"}

mkdir -p "${project_dir}/bench"

for p in ${proportions[@]}
do
    cmake -D READ_PROPORTION=${p} -D CMAKE_BUILD_TYPE=Release .
    make

    out_file="${project_dir}/bench/output_read${p}.log"
    mem_out_file="${project_dir}/bench/mem_read${p}.log"

    echo "Read Proportion: ${p}%" > "${out_file}"
    echo "Read Proportion: ${p}%" > "${mem_out_file}"
    for i in {1..10}
    do
        echo "Iteration $i" >> "${out_file}"
        echo "Iteration $i" >> "${mem_out_file}"

        bash -c 'while true; do (ps -eo rsz,cmd | grep IPP_HW5 | grep -v grep; sleep 1s); done' >> "${mem_out_file}" &
        trap 'kill -9 %1' EXIT
        stdbuf -o 0 "${project_dir}/bin/IPP_HW5" | tee -a "${out_file}"
        kill -9 %1

        echo "" >> "${out_file}"
        echo "" >> "${mem_out_file}"
    done
done
