Exercise Name: proxyServer.c, threadpool.c
Description:
	The server receives a request, checks it, and opens the file accordingly
	To compilate the program : enter "gcc proxyServer.c -o server -lpthread".
	To run the program: enter "./proxyServer <port> <pool-size> <max-number-of-request> <filter-file>"
Files description: 
	There are 2 files:
		(1)proxyServer.c - read the request, check it and send it to dispatch to execute.
			The proxy server recieves and validates an HTTP request from the client
			If it can handle it - forwards it to the host web server and returns a response back to the client.
			else, it's only sending a response to the client without.
			
		(2)threadpool.c - allocate the jobs to its pre-defined threads for executection