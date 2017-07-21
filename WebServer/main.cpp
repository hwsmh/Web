#include "server_http.hpp"
#include <iostream>
#include <thread>



int main(int argc, char** argv)
{
	server<HTTP> server(8181);

	server.resource_["1"]["2"] =
		[]()
	{
		printf("..\n");
	};
	server.resource_["3"]["4"] =
		[]()
	{
		printf("...\n");
	};
   
// 	for (auto res_onelayer : server.resource_)
// 	{
// 		std::cout <<res_onelayer.first<<std::endl;
// 		for (auto res_secondlayer : res_onelayer.second)
// 		{
// 			
// 			std::cout << res_secondlayer.first << std::endl;
// 			res_secondlayer.second();
// 		}
// 	}

	std::thread server_thread(
		[&server]() 
		{
			server.start();
		});

	server_thread.join();

	getchar();
	return 0;
}