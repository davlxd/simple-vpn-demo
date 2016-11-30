vpn : vpn.c
	gcc -o $@ $^ -g -Wall

clean :
	rm vpn
