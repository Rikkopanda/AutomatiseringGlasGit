#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

enum write_fd {
	STDIN,
	STDOUT,
	STDERR,
};

enum exit_status {
	OK,
	ERR,
};

typedef struct {
	int id;
	char *buf;
	size_t len;
} Client;

void fatal(void) {
	write(STDERR, "Fatal error\n", 12);
	exit(ERR);
}

char *str_join(char *buf, const char *data) {
	size_t buf_len = (buf ? strlen(buf) : 0);
	size_t data_len = strlen(data);
	char *new = realloc(buf, buf_len + data_len + 1);
	if (new == NULL) return free(buf), NULL;
	strcat(new, data);
	return new;
}

int main(int ac, char **av) {
	if (ac != 2)
		return write(STDERR, "Wrong number of arguments\n", 26), ERR;

	int server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) fatal();

	struct sockaddr_in servaddr = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons(atoi(av[1])),
	};

	if (bind(server_fd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
		fatal();
	if (listen(server_fd, 128) < 0) fatal();

	Client clients[1024] = {0}; // hardcoded
	int next_id = 0;
	fd_set active_fds, read_fds;
	FD_ZERO(&active_fds);
	FD_SET(server_fd, &active_fds);
	int max_fd = server_fd;

	char buf[102401]; // hardcoded

	while (1) {
		read_fds = active_fds;
		if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) fatal();
		for (int fd = 0; fd <= max_fd; fd++) {
			if (!FD_ISSET(fd, &read_fds)) continue;
			if (fd == server_fd) { // new client
				int client_fd = accept(server_fd, NULL, NULL);
				if (client_fd < 0) continue;
				if (client_fd > max_fd) max_fd = client_fd;
				FD_SET(client_fd, &active_fds);
				clients[client_fd].id = next_id++;
				clients[client_fd].buf = NULL;
				sprintf(buf, "server: client %d just arrived\n", clients[client_fd].id);
				for (int i = 0; i <= max_fd; i++) {
					if (FD_ISSET(i, &active_fds) && i != server_fd && i != client_fd)
						send(i, buf, strlen(buf), 0);
				}
			} else { // clients
				int bytes = recv(fd, buf, 102400, 0);
				if (bytes <= 0) { // DISCONNECT
					sprintf(buf, "client %d just left\n", clients[fd].id);
					for (int i = 0; i <= max_fd; i++) {
						if (FD_ISSET(i, &active_fds) && i != server_fd && i != fd)
							send(i, buf, strlen(buf), 0);
					}
					free(clients[fd].buf);
					FD_CLR(fd, &active_fds);
					close(fd);
				} else {
					buf[bytes] = 0;
					clients[fd].buf = str_join(clients[fd].buf, buf);
					if (clients[fd].buf == NULL) fatal();
					char prefix[32];
					sprintf(prefix, "client %d: ", clients[fd].id);
					char *ptr = clients[fd].buf;
					char *end;
					while ((end = strstr(ptr, "\n"))) {
						if (end == NULL) break;
						size_t line_len = end - ptr + 1;
						for (int i = 0; i <= max_fd; i++) {
							if (FD_ISSET(i, &active_fds) && i != server_fd && i != fd) {
								send(i, prefix, strlen(prefix), 0);
								send(i, ptr, line_len, 0);
							}
						}
						ptr = end + 1;
					}
					if (ptr != NULL) {
						size_t remain_len = strlen(ptr);
						memmove(clients[fd].buf, ptr, remain_len + 1);
						if (remain_len == 0) {
							free(clients[fd].buf);
							clients[fd].buf = NULL;
						} else {
							char *nb = realloc(clients[fd].buf, remain_len + 1);
							if (nb == NULL) fatal();
							clients[fd].buf = nb;
						}
					}
				}
			}
		}
	}
	return OK;
}
