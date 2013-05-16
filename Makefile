all:
	g++ -c -Wall allreduce.cc -o liballreduce.o
	ar rvs liballreduce.a liballreduce.o
	g++ -I . baseline.cc liballreduce.a -o baseline
	g++ -c -Wall spanning_graph.cc -o spanning_graph.o
	g++ -o spanning_graph spanning_graph.o
	

