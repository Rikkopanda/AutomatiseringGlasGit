#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
// socket
#include <sys/socket.h>
// inet
#include <netinet/in.h>

enum {
	STDIN,
	STDOUT,
	STDERR,
};

enum {
	OK,
	ERR,
};

void fatal(void) {
	write(STDERR, "Fatal error\n", 12);
	exit(ERR);
}

char *join(char *buf, const char *dat) {
	size_t buflen = (buf ? strlen(buf) : 0);
	size_t datlen = strlen(dat);
	char *new = realloc(buf, buflen + datlen + 1);
	if (!new) return free(buf), NULL;
	memcpy(new + buflen, dat, datlen + 1);
	return new;
}

int main(int ac, char **av) {
	if (ac != 2) return write(STDERR, "Wrong number of arguments\n", 26), ERR;

	int sv = socket(AF_INET, SOCK_STREAM, 0);
	if (sv < 0) fatal();

	struct sockaddr_in sva = {
		.sin_family = AF_INET,
		.sin_port = htons(atoi(av[1])),
		.sin_addr.s_addr = htonl(0x7f000001),
	};

	typedef const struct sockaddr addr;
	if (bind(sv, (addr*)&sva, sizeof(sva)) < 0) fatal();
	if (listen(sv, 128) < 0) fatal();

	typedef struct {
		int id;
		char *buf;
	} Client;

	Client clients[2048] = {0};
	int nid = 0;

	fd_set active, ready;
	FD_ZERO(&active);
	FD_SET(sv, &active);
	int max = sv;

	char buf[102401] = {0};

	while (1) {
		ready = active;
		if (select(max + 1, &ready, NULL, NULL, NULL) < 0) fatal();
		for (int fd = 0; fd <= max; fd++) {
			if (!FD_ISSET(fd, &ready)) continue;
			if (fd == sv) { // new
				int client = accept(sv, NULL, NULL);
				if (client < 0) continue;
				if (client > max) max = client;
				FD_SET(client, &active);
				clients[client].id = nid++;
				clients[client].buf = NULL;
				sprintf(buf, "server: client %d just arrived\n", clients[client].id);
				for (int i = 0; i <= max; i++) {
					if (FD_ISSET(i, &active) && i != sv && i != client)
						send(i, buf, strlen(buf), 0);
				}
			} else { // client
				int bytes = recv(fd, buf, 102400, 0);
				if (bytes <= 0) { // disconnect 
					sprintf(buf, "server: client %d just left\n", clients[fd].id);
					for (int i = 0; i <= max; i++) {
						if (FD_ISSET(i, &active) && i != sv && i != fd)
							send(i, buf, strlen(buf), 0);
					}
					free(clients[fd].buf);
					clients[fd].buf = NULL;
					FD_CLR(fd, &active);
					close(fd);
				} else { //
					buf[bytes] = 0;
					clients[fd].buf = join(clients[fd].buf, buf);
					if (clients[fd].buf == NULL) fatal();
					char prefix[64] = {0};
					sprintf(prefix, "client %d: ", clients[fd].id);
					char *ptr = clients[fd].buf;
					char *end = 0;
					while ((end = strstr(ptr, "\n"))) {
						size_t len = end - ptr + 1;
						for (int i = 0; i <= max; i++) {
							if (FD_ISSET(i, &active) && i != sv && i != fd) {
								send(i, prefix, strlen(prefix), 0);
								send(i, ptr, len, 0);
							}
						}
						ptr = end + 1;
					}
					if (!ptr[0]) {
						free(clients[fd].buf);
						clients[fd].buf = NULL;
					} else if (ptr != clients[fd].buf) {
						strcpy(clients[fd].buf, ptr);
					}
				}
			}
		}
	}
	return OK;
}
