#!/bin/bash

rho=( 0.84 0.845 0.86 0.9 )
l_s=( 0.003 0.01 0.03 0.05 0.1 0.3 )
l_n=( 0.01 0.1 0.3 0.4 0.5 0.53 0.55 0.6 0.7 0.8 1.0 )

run=0
ID="MSD1024Array8"

for i in ${rho[@]}
do
	for j in ${l_n[@]}
	do
		for k in ${l_s[@]}
		do
		        ~/remote/oldResults/code/plot/plot_jam_act_03.gnu run$run $ID
			run=$((run+1))
		done
	done
done
exit

