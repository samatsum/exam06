#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

// write, close, select, socket, accept, listen, send, recv,
// bind, strstr, malloc, realloc, free, calloc, bzero, atoi,
// sprintf, strlen, exit, strcpy, strcat, memset, htonl, htons
// FD_ZERO / FD_SET / FD_ISSET / FD_CLR は関数ではなく <sys/select.h> のマクロ。
// select() に渡す fd_set を操作する道具、とまとめて覚える。

// FD_ZERO   : 名簿を空にする
// FD_SET    : fd を名簿に入れる
// select    : 名簿を渡して、反応がある fd を待つ
// FD_ISSET  : どの fd が反応したか確認する
// FD_CLR    : 切断した fd を名簿から消す

/* ************************************************************************** */

// #define 禁止なので、名前付き定数は enum でまとめる。
enum e_server_const {
	MESSAGE_SIZE = 64,
	LISTEN_BACKLOG = 128,
	READ_SIZE = 4096
};

// クライアント1人分。input は受信途中、output は送信待ちの文字列。
typedef struct s_client {
	int				fd;
	int				id;
	char*			input;
	char*			output;
	struct s_client*	next;
} t_client;

// サーバ全体。active_fds は「今生きていて監視したい fd の名簿」。
typedef struct s_server {
	int			listen_fd;
	int			next_id;
	int			max_fd;
	t_client*	clients;
	fd_set		active_fds;
} t_server;

/* ************************************************************************** */

// 1ファイル提出なので、先に static 関数のプロトタイプを並べる。
static void
	server_init(t_server* server, int port);
static void
	server_loop(t_server* server);
static int
	create_listener(int port);
static void
	prepare_write_fds(t_server* server, fd_set* write_fds);
static void
	add_client(t_server* server);
static void
	read_client(t_server* server, int fd);
static void
	write_client(t_server* server, int fd);
static void
	process_input(t_server* server, t_client* client);
static void
	broadcast_line(t_server* server, t_client* sender, char* line);
static void
	disconnect_client(t_server* server, int fd);
static void
	remove_client(t_server* server, int fd);
static void
	broadcast(t_server* server, int except_fd, const char* msg);
static void
	queue_message(t_client* client, const char* msg);
static void
	drop_sent_prefix(t_client* client, int sent);
static t_client*
	find_client(t_server* server, int fd);
static void
	rebuild_max_fd(t_server* server);
static char*
	take_line(char** pending);
static int
	append_text(char** dst, const char* src);
static int
	find_newline(const char* str);
static char*
	xstrdup(const char* src);
static void
	put_error(const char* msg);
static void
	fatal_error(void);

/* ************************************************************************** */
// 引数を検証し、サーバを初期化してイベントループへ入る。
int
	main(int argc, char** argv)
{
	t_server	server;

	if (argc != 2) {
		put_error("Wrong number of arguments\n");
		exit(1);
	}
	server_init(&server, atoi(argv[1]));
	server_loop(&server);
	return (0);
}

/* ************************************************************************** */
// listen 用 fd を作り、select() で監視する fd_set を初期化する。
static void
	server_init(t_server* server, int port)
{
	// 構造体をゼロ初期化すると、clients/input/output は NULL から始まる。
	memset(server, 0, sizeof(*server));
	// active_fds を空にしてから、listen_fd だけを最初の監視対象にする。
	FD_ZERO(&server->active_fds);
	server->listen_fd = create_listener(port);
	server->max_fd = server->listen_fd;
	FD_SET(server->listen_fd, &server->active_fds);
}

/* ************************************************************************** */
// select() で新規接続、受信可能 fd、送信可能 fd を順番に処理する。
static void
	server_loop(t_server* server)
{
	fd_set		read_fds;
	fd_set		write_fds;
	t_client*	client;
	t_client*	next;

	while (1) {
		// active_fds は本物の名簿。select() は渡された fd_set を書き換える。
		// だから毎回コピーを作り、コピーの方を select() に渡す。
		read_fds = server->active_fds;
		// 書き込み監視は output がある client だけ。空なら send する物がない。
		prepare_write_fds(server, &write_fds);
		// ここで、どれかの fd が読める/書ける状態になるまで待つ。
		if (select(server->max_fd + 1, &read_fds, &write_fds, NULL, NULL) < 0) {
			fatal_error();
		}
		// listen_fd が読めるなら、新しい接続が来た合図。accept() する。
		if (FD_ISSET(server->listen_fd, &read_fds)) {
			add_client(server);
		}
		client = server->clients;
		while (client) {
			// read_client() 内で client が消える可能性があるので先に next を避難。
			next = client->next;
			// client fd が読めるなら、相手が送信したか切断したということ。
			if (FD_ISSET(client->fd, &read_fds)) {
				read_client(server, client->fd);
			}
			client = next;
		}
		client = server->clients;
		while (client) {
			next = client->next;
			// client fd が書けるなら、貯めていた output を送れる。
			if (FD_ISSET(client->fd, &write_fds)) {
				write_client(server, client->fd);
			}
			client = next;
		}
	}
}

/* ************************************************************************** */
// 127.0.0.1 の指定ポートで待ち受けるソケットを作る。
static int
	create_listener(int port)
{
	struct sockaddr_in	addr;
	int					fd;

	// AF_INET は IPv4、SOCK_STREAM は TCP。ここで待ち受け用 fd を作る。
	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		fatal_error();
	}
	// sockaddr_in に「IPv4 / 127.0.0.1 / 指定 port」という住所を詰める。
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);
	// bind は「この fd をこの IP:port で使う」と OS に登録する。
	if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		fatal_error();
	}
	// listen 後、この fd は accept() で新規接続を受け取れるようになる。
	if (listen(fd, LISTEN_BACKLOG) < 0) {
		fatal_error();
	}
	return (fd);
}

/* ************************************************************************** */
// output に未送信データがあるクライアントだけ write_fds に入れる。
static void
	prepare_write_fds(t_server* server, fd_set* write_fds)
{
	t_client*	client;

	// write_fds は毎回作り直す。送信待ちがある fd だけ入れるため。
	FD_ZERO(write_fds);
	client = server->clients;
	while (client) {
		if (client->output) {
			FD_SET(client->fd, write_fds);
		}
		client = client->next;
	}
}

/* ************************************************************************** */
// accept() したクライアントへ ID を付け、既存クライアントへ入室通知を積む。
static void
	add_client(t_server* server)
{
	t_client*	client;
	char		msg[MESSAGE_SIZE];
	int			fd;

	// accept は新しい client 専用 fd を返す。listen_fd とは別物。
	fd = accept(server->listen_fd, NULL, NULL);
	if (fd < 0) {
		fatal_error();
	}
	// calloc なので input/output/next は NULL 初期化される。
	client = calloc(1, sizeof(t_client));
	if (!client) {
		close(fd);
		fatal_error();
	}
	client->fd = fd;
	// ID は 0, 1, 2... と増える。切断されても再利用しない。
	client->id = server->next_id++;
	// 連結リストの先頭に追加する。順番は評価出力に影響しない。
	client->next = server->clients;
	server->clients = client;
	// 次回以降 select() でこの client fd も読む対象にする。
	FD_SET(fd, &server->active_fds);
	if (fd > server->max_fd) {
		server->max_fd = fd;
	}
	sprintf(msg, "server: client %d just arrived\n", client->id);
	broadcast(server, fd, msg);
}

/* ************************************************************************** */
// recv() した文字列を input に足し、完成した行だけ broadcast する。
static void
	read_client(t_server* server, int fd)
{
	t_client*	client;
	char		buf[READ_SIZE + 1];
	int			bytes;

	client = find_client(server, fd);
	if (!client) {
		return ;
	}
	// recv が 0 以下なら、相手が切断したか通信エラー。退出扱いにする。
	bytes = recv(fd, buf, READ_SIZE, 0);
	if (bytes <= 0) {
		disconnect_client(server, fd);
		return ;
	}
	// TCP は行単位では届かないので、受け取った分をまず input に足す。
	buf[bytes] = '\0';
	if (!append_text(&client->input, buf)) {
		fatal_error();
	}
	process_input(server, client);
}

/* ************************************************************************** */
// output の送信待ちデータを send() し、送れた分だけ削る。
static void
	write_client(t_server* server, int fd)
{
	t_client*	client;
	int			sent;

	client = find_client(server, fd);
	if (!client || !client->output) {
		return ;
	}
	// send は output 全部を一度に送れるとは限らない。戻り値が送れたバイト数。
	sent = send(fd, client->output, strlen(client->output), 0);
	if (sent <= 0) {
		disconnect_client(server, fd);
		return ;
	}
	drop_sent_prefix(client, sent);
}

/* ************************************************************************** */
// input から '\n' までの行を取り出せるだけ処理する。
static void
	process_input(t_server* server, t_client* client)
{
	char*	line;

	while (client->input) {
		// 改行がまだ無ければ NULL が返る。その場合は次の recv を待つ。
		line = take_line(&client->input);
		if (!line) {
			return ;
		}
		broadcast_line(server, client, line);
		free(line);
	}
}

/* ************************************************************************** */
// 1行の先頭に client ID を付け、送信者以外へ送信待ちとして積む。
static void
	broadcast_line(t_server* server, t_client* sender, char* line)
{
	char	prefix[MESSAGE_SIZE];
	char*	msg;

	sprintf(prefix, "client %d: ", sender->id);
	// +2 は末尾に足す \n と \0 の分。
	msg = malloc(strlen(prefix) + strlen(line) + 2);
	if (!msg) {
		fatal_error();
	}
	strcpy(msg, prefix);
	strcat(msg, line);
	strcat(msg, "\n");
	broadcast(server, sender->fd, msg);
	free(msg);
}

/* ************************************************************************** */
// 切断したクライアントを消し、残りのクライアントへ退出通知を積む。
static void
	disconnect_client(t_server* server, int fd)
{
	t_client*	client;
	char		msg[MESSAGE_SIZE];

	client = find_client(server, fd);
	if (!client) {
		return ;
	}
	sprintf(msg, "server: client %d just left\n", client->id);
	remove_client(server, fd);
	broadcast(server, fd, msg);
}

/* ************************************************************************** */
// fd_set とリストからクライアントを外し、fd と動的メモリを解放する。
static void
	remove_client(t_server* server, int fd)
{
	t_client*	client;
	t_client*	prev;

	client = server->clients;
	prev = NULL;
	while (client) {
		if (client->fd == fd) {
			if (prev) {
				prev->next = client->next;
			} else {
				server->clients = client->next;
			}
			// 閉じた fd を select() しないように、名簿から外してから close。
			FD_CLR(client->fd, &server->active_fds);
			close(client->fd);
			free(client->input);
			free(client->output);
			free(client);
			if (fd == server->max_fd) {
				rebuild_max_fd(server);
			}
			return ;
		}
		prev = client;
		client = client->next;
	}
}

/* ************************************************************************** */
// except_fd 以外の全クライアントへ同じメッセージを積む。
static void
	broadcast(t_server* server, int except_fd, const char* msg)
{
	t_client*	client;

	client = server->clients;
	while (client) {
		if (client->fd != except_fd) {
			queue_message(client, msg);
		}
		client = client->next;
	}
}

/* ************************************************************************** */
// クライアントごとの送信待ちバッファ output に文字列を追加する。
static void
	queue_message(t_client* client, const char* msg)
{
	if (!append_text(&client->output, msg)) {
		fatal_error();
	}
}

/* ************************************************************************** */
// send() 済みの先頭部分を output から取り除く。
static void
	drop_sent_prefix(t_client* client, int sent)
{
	char*	rest;

	if (!client->output) {
		return ;
	}
	// 全部送れたなら output を空にする。残りがあるなら先頭だけ削る。
	if ((size_t)sent >= strlen(client->output)) {
		free(client->output);
		client->output = NULL;
		return ;
	}
	rest = xstrdup(client->output + sent);
	if (!rest) {
		fatal_error();
	}
	free(client->output);
	client->output = rest;
}

/* ************************************************************************** */
// fd に対応するクライアント構造体を連結リストから探す。
static t_client*
	find_client(t_server* server, int fd)
{
	t_client*	client;

	client = server->clients;
	while (client) {
		if (client->fd == fd) {
			return (client);
		}
		client = client->next;
	}
	return (NULL);
}

/* ************************************************************************** */
// 最大 fd が閉じられた後、select() 用の max_fd を再計算する。
static void
	rebuild_max_fd(t_server* server)
{
	t_client*	client;

	server->max_fd = server->listen_fd;
	client = server->clients;
	while (client) {
		if (client->fd > server->max_fd) {
			server->max_fd = client->fd;
		}
		client = client->next;
	}
}

/* ************************************************************************** */
// pending から完成済みの1行を切り出し、未完成分だけ pending に残す。
static char*
	take_line(char** pending)
{
	char*	line;
	char*	rest;
	int		newline;
	int		i;

	if (!pending || !*pending) {
		return (NULL);
	}
	// pending に改行がある時だけ、1行として broadcast できる。
	newline = find_newline(*pending);
	if (newline < 0) {
		return (NULL);
	}
	line = malloc(newline + 1);
	if (!line) {
		fatal_error();
	}
	i = 0;
	while (i < newline) {
		line[i] = (*pending)[i];
		i++;
	}
	line[i] = '\0';
	// 改行の後ろは、次の行の途中かもしれないので pending に戻す。
	rest = xstrdup(*pending + newline + 1);
	if (!rest) {
		fatal_error();
	}
	free(*pending);
	if (rest[0] == '\0') {
		free(rest);
		rest = NULL;
	}
	*pending = rest;
	return (line);
}

/* ************************************************************************** */
// dst の末尾に src を連結し、古い dst は解放する。
static int
	append_text(char** dst, const char* src)
{
	char*	joined;
	size_t	old_len;
	size_t	add_len;

	old_len = 0;
	if (*dst) {
		old_len = strlen(*dst);
	}
	add_len = strlen(src);
	// 既存分 + 追加分 + 終端 NUL のサイズを確保する。
	joined = malloc(old_len + add_len + 1);
	if (!joined) {
		return (0);
	}
	joined[0] = '\0';
	if (*dst) {
		strcpy(joined, *dst);
	}
	strcat(joined, src);
	free(*dst);
	*dst = joined;
	return (1);
}

/* ************************************************************************** */
// 文字列中の最初の改行位置を返す。見つからなければ -1 を返す。
static int
	find_newline(const char* str)
{
	int	i;

	i = 0;
	while (str[i]) {
		if (str[i] == '\n') {
			return (i);
		}
		i++;
	}
	return (-1);
}

/* ************************************************************************** */
// allowed functions だけで使える小さな strdup 互換関数。
static char*
	xstrdup(const char* src)
{
	char*	dst;

	dst = malloc(strlen(src) + 1);
	if (!dst) {
		return (NULL);
	}
	strcpy(dst, src);
	return (dst);
}

/* ************************************************************************** */
// 標準エラーへ指定文字列を出力する。
static void
	put_error(const char* msg)
{
	write(2, msg, strlen(msg));
}

/* ************************************************************************** */
// fatal error を出して即終了する。
static void
	fatal_error(void)
{
	put_error("Fatal error\n");
	exit(1);
}
