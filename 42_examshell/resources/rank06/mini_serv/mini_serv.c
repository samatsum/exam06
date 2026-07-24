#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * 読む順番は main -> create_listener -> run_server。
 * run_server の中で、接続・受信・送信・切断の4イベントを処理する。
 *
 * listen_fd : 新規接続を受け付ける fd
 * client fd : 接続した相手一人と通信する fd
 * active_fds: 現在監視している fd の名簿
 * read_fds  : 今回、読み込み可能だった fd の名簿
 * write_fds : 今回、書き込み可能だった fd の名簿
 *
 * FD_ZERO=名簿を空にする、FD_SET=追加、FD_ISSET=確認、FD_CLR=削除。
 * select は渡した名簿を書き換えるので、active_fds のコピーを渡す。
 * man 2 select / socket / bind / listen / accept / recv / send、man 7 ip
 */

/* ************************************************************************** */

enum e_server_const {
	LISTEN_BACKLOG = 128,
	BUFFER_SIZE = 4096,
	MESSAGE_SIZE = 64
};

// input は未完成の受信行、output はまだ send できていない文字列。
typedef struct s_client {
	int		id;
	char*	input;
	char*	output;
} t_client;

// clients[fd] として使うため、fd からクライアントをすぐ取得できる。
typedef struct s_server {
	int			listen_fd;
	int			max_fd;
	int			next_id;
	fd_set		active_fds;
	t_client	clients[FD_SETSIZE];
} t_server;

/* ************************************************************************** */

static int
	create_listener(int port);
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
static void
	append_text(char** dst, const char* src);
static void
	fatal_error(void);

/* ************************************************************************** */
// 引数を確認し、最初は接続受付用 fd だけを監視して開始する。
int
	main(int argc, char** argv)
{
	t_server	server;

	if (argc != 2) {
		write(2, "Wrong number of arguments\n", 26);
		return (1);
	}
	memset(&server, 0, sizeof(server));
	server.listen_fd = create_listener(atoi(argv[1]));
	server.max_fd = server.listen_fd;
	FD_ZERO(&server.active_fds);
	FD_SET(server.listen_fd, &server.active_fds);
	run_server(&server);
	return (0);
}

/* ************************************************************************** */
// socket -> bind -> listen で、127.0.0.1:port の受付を作る。
static int
	create_listener(int port)
{
	struct sockaddr_in	address;
	int					fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		fatal_error();
	}
	/* sockaddr_in は IPv4 の住所（man 7 ip）。
	 * htonl/htons は数値を通信で使うバイト順へ変換する。 */
	bzero(&address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(port);
	// bind は fd に住所を登録する。引数型に合わせ sockaddr* へ変換する。
	if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		fatal_error();
	}
	if (listen(fd, LISTEN_BACKLOG) < 0) {
		fatal_error();
	}
	return (fd);
}

/* ************************************************************************** */
// select で反応を待ち、可能になった fd の処理だけを行う。
static void
	run_server(t_server* server)
{
	fd_set	read_fds;
	fd_set	write_fds;
	int		fd;

	while (1) {
		read_fds = server->active_fds;
		FD_ZERO(&write_fds);
		fd = 0;
		while (fd <= server->max_fd) {
			if (fd != server->listen_fd
				&& FD_ISSET(fd, &server->active_fds)
				&& server->clients[fd].output) {
				FD_SET(fd, &write_fds);
			}
			fd++;
		}
		if (select(server->max_fd + 1, &read_fds,
				&write_fds, NULL, NULL) < 0) {
			fatal_error();
		}
		// listen_fd が読めるのは、新しい接続が待っている合図。
		if (FD_ISSET(server->listen_fd, &read_fds)) {
			accept_client(server);
		}
		fd = 0;
		while (fd <= server->max_fd) {
			if (fd != server->listen_fd
				&& FD_ISSET(fd, &server->active_fds)) {
				if (FD_ISSET(fd, &read_fds)) {
					receive_client(server, fd);
				}
				// receive_client で切断されていない場合だけ送信する。
				if (FD_ISSET(fd, &server->active_fds)
					&& FD_ISSET(fd, &write_fds)) {
					send_client(server, fd);
				}
			}
			fd++;
		}
	}
}

/* ************************************************************************** */
// accept でクライアント専用 fd を受け取り、IDと監視を追加する。
static void
	accept_client(t_server* server)
{
	char	message[MESSAGE_SIZE];
	int		fd;

	fd = accept(server->listen_fd, NULL, NULL);
	if (fd < 0 || fd >= FD_SETSIZE) {
		if (fd >= 0) {
			close(fd);
		}
		fatal_error();
	}
	server->clients[fd].id = server->next_id++;
	FD_SET(fd, &server->active_fds);
	if (fd > server->max_fd) {
		server->max_fd = fd;
	}
	sprintf(message, "server: client %d just arrived\n",
		server->clients[fd].id);
	broadcast(server, fd, message);
}

/* ************************************************************************** */
// recv した文字を input へ足し、完成した各行を全員へ配信する。
static void
	receive_client(t_server* server, int fd)
{
	t_client*	client;
	char*		newline;
	char*		remaining;
	char		buffer[BUFFER_SIZE + 1];
	char		prefix[MESSAGE_SIZE];
	int			received;

	client = &server->clients[fd];
	received = recv(fd, buffer, BUFFER_SIZE, 0);
	if (received <= 0) {
		disconnect_client(server, fd);
		return ;
	}
	buffer[received] = '\0';
	// TCPでは1行が分割されて届くことがあるため、まず input へ貯める。
	append_text(&client->input, buffer);
	newline = strstr(client->input, "\n");
	while (newline) {
		*newline = '\0';
		sprintf(prefix, "client %d: ", client->id);
		broadcast(server, fd, prefix);
		broadcast(server, fd, client->input);
		broadcast(server, fd, "\n");
		// 改行より後ろは、次の行または未完成行として残す。
		remaining = NULL;
		append_text(&remaining, newline + 1);
		free(client->input);
		client->input = remaining;
		newline = strstr(client->input, "\n");
	}
}

/* ************************************************************************** */
// output を送れる分だけ送り、残りがあれば次回用に保存する。
static void
	send_client(t_server* server, int fd)
{
	t_client*	client;
	char*		remaining;
	size_t		length;
	ssize_t		sent;

	client = &server->clients[fd];
	length = strlen(client->output);
	sent = send(fd, client->output, length, 0);
	if (sent <= 0) {
		disconnect_client(server, fd);
		return ;
	}
	// send は一度で全データを送るとは限らない。
	if ((size_t)sent == length) {
		free(client->output);
		client->output = NULL;
	} else {
		remaining = NULL;
		append_text(&remaining, client->output + sent);
		free(client->output);
		client->output = remaining;
	}
}

/* ************************************************************************** */
// 切断した fd を名簿から外し、メモリと fd を解放する。
static void
	disconnect_client(t_server* server, int fd)
{
	char	message[MESSAGE_SIZE];
	int		id;

	id = server->clients[fd].id;
	FD_CLR(fd, &server->active_fds);
	close(fd);
	free(server->clients[fd].input);
	free(server->clients[fd].output);
	server->clients[fd].input = NULL;
	server->clients[fd].output = NULL;
	sprintf(message, "server: client %d just left\n", id);
	broadcast(server, -1, message);
}

/* ************************************************************************** */
// except_fd 以外の全クライアントへ送信待ち文字列を追加する。
static void
	broadcast(t_server* server, int except_fd, const char* text)
{
	int	fd;

	fd = 0;
	while (fd <= server->max_fd) {
		if (fd != server->listen_fd && fd != except_fd
			&& FD_ISSET(fd, &server->active_fds)) {
			append_text(&server->clients[fd].output, text);
		}
		fd++;
	}
}

/* ************************************************************************** */
// *dst の末尾へ src を連結する。realloc(NULL, n) は malloc(n) と同じ。
static void
	append_text(char** dst, const char* src)
{
	char*	joined;
	size_t	old_length;

	old_length = 0;
	if (*dst) {
		old_length = strlen(*dst);
	}
	joined = realloc(*dst, old_length + strlen(src) + 1);
	if (!joined) {
		fatal_error();
	}
	strcpy(joined + old_length, src);
	*dst = joined;
}

/* ************************************************************************** */
// 致命的エラーを標準エラーへ出して終了する。
static void
	fatal_error(void)
{
	write(2, "Fatal error\n", 12);
	exit(1);
}
