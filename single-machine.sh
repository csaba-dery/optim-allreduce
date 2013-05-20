#!/bin/sh
# A simplest-possible example of parallel code at work on a single machine.

killall baseline
killall spanning_graph
cat /dev/null > output.log

./spanning_graph > graph.log 2>&1


for i in {0..6} 
do
	#./baseline 8 $i > output.log 2>&1 &
	./baseline 8 $i 1>> output.log 2>/dev/null &
done
#echo "last"
./baseline 8 7 1>> output.log 2>/dev/null

#killall spanning_graph

