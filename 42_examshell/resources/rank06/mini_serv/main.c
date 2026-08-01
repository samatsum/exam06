#include <arpa/inet.h>	// htonl、htons
#include <netinet/in.h>	// sockaddr_in、INADDR_LOOPBACK
#include <stdio.h>	// sprintf
#include <stdlib.h>	// atoi、calloc、malloc、free、exit
#include <string.h>	// bzero、memset、strlen、strcpy、strcat
#include <sys/select.h>	// fd_set、select、FD系マクロ
#include <sys/socket.h>	// socket、bind、listen、accept、recv、send
#include <unistd.h>	// write、close



typedef struct s_client
{
	int id;
	char* input;
	char* output;
} t_client;

typedef struct s_server
{
	int next_id;
	int max_fd;
	int listner_fd;
	fd_set	active_fds;
	t_client clients[1024];
}	t_server;


static void
	run_server(t_server* server);
static void
	accept_client(t_server* server);
static void
	receive_client(t_server* server, int fd);
static void
	send_client(t_server* server, int fd);
static void
	disconnect_client(t_server* server, int fd);
static void
	broadcast(t_server* server, int except_fd, const char* text);
static int
	extract_message(char** buf, char** message);
static char*
	str_join(char* buf, const char* add);
static int
	create_listener(int port);
static void
	fatal_error(void);


static void
	fatal_error(void)
{
	write(2, "Fatal error\n", 12);	// 標準エラーへ固定文言を出す
	exit(1);	// ステータス1で即終了
}


int extract_message(char **buf, char **msg)
{
	char	*newbuf;
	int	i;

	*msg = 0;
	if (*buf == 0)
		return (0);
	i = 0;
	while ((*buf)[i])
	{
		if ((*buf)[i] == '\n')
		{
			newbuf = calloc(1, sizeof(*newbuf) * (strlen(*buf + i + 1) + 1));
			if (newbuf == 0)
				return (-1);
			strcpy(newbuf, *buf + i + 1);
			*msg = *buf;
			(*msg)[i + 1] = 0;
			*buf = newbuf;
			return (1);
		}
		i++;
	}
	return (0);
}

char *str_join(char *buf, char *add)
{
	char	*newbuf;
	int		len;

	if (buf == 0)
		len = 0;
	else
		len = strlen(buf);
	newbuf = malloc(sizeof(*newbuf) * (len + strlen(add) + 1));
	if (newbuf == 0)
		return (0);
	newbuf[0] = 0;
	if (buf != 0)
		strcat(newbuf, buf);
	free(buf);
	strcat(newbuf, add);
	return (newbuf);
}


/* ************************************************************************** */
// 引数を確認し、最初は接続受付用 fd だけを監視して開始する。
int
	main(int argc, char** argv)
{
	t_server	server;

	if (argc != 2) {	// 引数はポート番号1個だけ必要
		write(2, "Wrong number of arguments\n", 26);	// 標準エラーへ出力
		return (1);	// エラー終了
	}
	memset(&server, 0, sizeof(server));
	server.listner_fd = create_listener(atoi(argv[1]));
	server.max_fd = server.listner_fd;
	FD_ZERO(&server.active_fds);
	FD_SET(server.listner_fd, &server.active_fds);
	run_server(&server);
	return (0);	// 通常は無限ループなので到達しない
}


int create_listner(int port) {
	int sockfd;
	struct sockaddr_in servaddr; 

	// socket create and verification 
	sockfd = socket(AF_INET, SOCK_STREAM, 0); 
	if (sockfd == -1) { 
		printf("socket creation failed...\n"); 
		fatal_error();
	} 
	else
		printf("Socket successfully created..\n"); 
	bzero(&servaddr, sizeof(servaddr)); 

	// assign IP, PORT 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(2130706433); //127.0.0.1
	servaddr.sin_port = htons(8081); 
  
	// Binding newly created socket to given IP and verification 
	if ((bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) { 
		printf("socket bind failed...\n"); 
		fatal_error();
	} 
	else
		printf("Socket successfully binded..\n");
	if (listen(sockfd, 10) != 0) {
		printf("cannot listen\n"); 
		fatal_error();
	}
	else
		printf("Success listen %d\n", sockfd);
	return(sockfd);
}

void	run_server(t_server *server)
{
	fd_set read_fds;
	fd_set write_fds;
	int fd;

	while(1)
	{
		fd = 0;
		read_fds = server->active_fds;
		FD_ZERO(&write_fds);
		while(fd <= server->max_fd)
		{
			if (fd != server->listner_fd
				&& FD_ISSET(fd, &server->active_fds)
				&& server->clients[fd].output)
			{
				FD_ISSET(fd, &write_fds);
			}
			fd++;
		}
		fd = 0;
		if (select(server->max_fd+1, &write_fds, &read_fds, NULL, NULL) < 0){
			fatal_error();
		}
		if (FD_ISSET(server->listner_fd, &server->active_fds)){
			accept_client(server);
		}
		fd = 0;
		while(fd <= server->max_fd)
		{
			if (fd != server->listner_fd && FD_ISSET(fd, &read_fds))
			{
				receive_client(server, fd);
			}
			if (fd != server->listner_fd && FD_ISSET(fd, &write_fds))
			{
				send_client(server, fd);
			}
			fd++;
		}

	}
}



void	accept_client(t_server* server)
{
	int accept_fd;
	char accept_message[42];

	accept_fd = accept(server->listner_fd, NULL, NULL);
	if (accept_fd < 0 || accept_fd >= 1024) {
		if (accept_fd >= 1024)
			close(accept_fd); 
        printf("server acccept failed...\n"); 
        fatal_error();
    } 
    else
        printf("server acccept the client...\n");
	server->clients[accept_fd].id = server->next_id;
	server->next_id++;
	FD_SET(accept_fd, &server->active_fds);
	if (accept_fd >= server->max_fd){
		server->max_fd = accept_fd;
	}
	sprintf(&accept_message, "server: client %d just arrived\n", server->clients[accept_fd].id);
	broadcast(server, -1, accept_message);
}

void	broadcast(t_server *server, int expect_fd, char* message)
{
	int fd;

	fd = 0;
	while(fd <= server->max_fd)
	{
		if(fd != expect_fd
			&& FD_ISSET(fd, &server->active_fds)
			&& fd != server->listner_fd)
		{
			server->clients[fd].output = str_join(server->clients[fd].output, message);
			if (server->clients[fd].output == NULL){
				fatal_error();
			}
		}
		fd++;
	}
}
