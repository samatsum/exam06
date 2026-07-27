#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>

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

typedef struct s_client
{
    int id;
    char* input;
    char* ouput;
} t_client;

typedef struct s_server
{
    int listen_fd;
    int max_fd;
    int next_id;
    fd_set active_fds;
    t_client clients[1024];
} t_server;


int main(int argc, char**argv) {

    if (argc != 2)
    {
        write(2, "Wrong number of arguments\n", 26);
        exit(1);
    }

    t_server server;

    memset(&server, 0 ,sizeof(server));
    server.listen_fd = create_listn_fd(atoi(argv[1]));
    server.max_fd = server.listen_fd;
    FD_ZERO(&server.active_fds);
    FD_SET(server.listen_fd, &server.active_fds);
    run_server(&server);
    return (0);
}

int create_listn_fd(int port)
{
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
	servaddr.sin_port = htons(port); 
  
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
        printf("listen start!\n");
    return(sockfd);
}

void    fatal_error(void)
{
    write(2, "Fatal error\n", 12);
    exit(1);
}


void	run_server(t_server *server)
{
	fd_set	read_fds;
	fd_set	write_fds;
	int		fd;

	while (1)
	{
		/* selectに渡す名簿を準備する */

		/* selectで反応を待つ */

		/* listen_fdなら新しい接続 */

		/* client fdなら受信または送信 */
	}
}