#include <iostream>
#include <stdlib.h>
#include <allreduce.h>

using namespace std; 

int main(int argc, char* argv[]) {

	//uint32_t length = 1 << 18;
	uint32_t length = 128;
	float* buffer = new float[length];
	int total_nodes = atoi(argv[1]);
	int node_number = atoi(argv[2]);
	
	/*for (int i = 0; i < length; i++) {
		buffer[i] = (i+1)*(node_number+1);
	}*/
	std::fill_n(buffer, length, 1);
	/*cout << "Start for node " << node_number << endl;
	for (int i=0; i< length; i++) {
		cout << buffer[i] << endl;
	}
  	cout << endl;*/

	node_socks* sock0 = new node_socks;	

	//cout << "First element before:" << buffer[0] << endl;

	all_reduce(buffer, length, "localhost", 0, total_nodes, node_number, *sock0);

	//cout << "Final for node " << node_number << endl;
	for (int i=0; i< length; i++) {
		cout << buffer[i] << " ";
	}	
  	cout << endl;
	
	delete[] buffer;
	delete sock0;
}
