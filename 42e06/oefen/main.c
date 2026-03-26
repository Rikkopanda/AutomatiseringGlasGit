

#include <sys/socket.h>
#include <sys/types.h>

int main(int ac, char **av) {

	struct sockaddr *addr;
	int ret = bind(av[1], addr, sizeof(addr));

}