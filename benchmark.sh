#!/bin/bash

echo "Benchmark"

files=`ls sudoku*.txt`
echo $files
for f in $files
do
	totalTime=0
	for i in {0..49}
	do
		curTime=`./sudoku $f`
		totalTime=$((totalTime + curTime))
	done
	meanTime=$((totalTime/50))
	echo "${f} : ${meanTime}"
done

	
