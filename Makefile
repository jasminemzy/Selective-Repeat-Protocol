default:
	gcc -o server server.c -w -lpthread
	gcc -o client client.c -w
	gcc -o test ctest.c -w