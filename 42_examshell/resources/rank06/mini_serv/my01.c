#include <arpa/inet.h>	// htonl、htons
#include <netinet/in.h>	// sockaddr_in、INADDR_LOOPBACK
#include <stdio.h>	// sprintf
#include <stdlib.h>	// atoi、calloc、malloc、free、exit
#include <string.h>	// bzero、memset、strlen、strcpy、strcat
#include <sys/select.h>	// fd_set、select、FD系マクロ
#include <sys/socket.h>	// socket、bind、listen、accept、recv、send
#include <unistd.h>	// write、close

/*
 * 読む順番は main -> create_listener -> run_server。
 * run_server の中で、接続・受信・送信・切断の4イベントを処理する。
 * 配布main.cの extract_message / str_join / socket部分を再利用している。
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
	LISTEN_BACKLOG = 128,	// 接続待ちキューへ置ける数
	BUFFER_SIZE = 4096,	// 1回のrecvで読む最大サイズ
	MESSAGE_SIZE = 64	// 接続・切断通知などの短い文字列用
};

// input は未完成の受信行、output はまだ send できていない文字列。
typedef struct s_client {
	int		id;	// 接続順に割り当てる0, 1, 2...
	char*	input;	// まだ改行まで完成していない受信文字列
	char*	output;	// まだsendし終えていない文字列
} t_client;

// clients[fd] として使うため、fd からクライアントをすぐ取得できる。
typedef struct s_server {
	int			listen_fd;	// 新規接続を受け付けるfd
	int			max_fd;	// selectが調べる最大のfd
	int			next_id;	// 次のクライアントへ渡すID
	fd_set		active_fds;	// 現在監視しているfdの名簿
	t_client	clients[FD_SETSIZE];	// fdを添字にしたクライアント表
} t_server;


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
	memset(&server, 0, sizeof(server));	// 数値を0、ポインタをNULLで開始
	server.listen_fd = create_listener(atoi(argv[1]));	// 受付用fdを作る
	server.max_fd = server.listen_fd;	// 最初の最大fdは受付用fd
	FD_ZERO(&server.active_fds);	// 監視名簿を空にする
	FD_SET(server.listen_fd, &server.active_fds);	// 受付用fdを監視する
	run_server(&server);	// selectの無限ループへ入る
	return (0);	// 通常は無限ループなので到達しない
}

/* ************************************************************************** */
// socket -> bind -> listen で、127.0.0.1:port の受付を作る。
static int
	create_listener(int port)
{
	struct sockaddr_in	address;
	int					fd;

	fd = socket(AF_INET, SOCK_STREAM, 0);	// IPv4/TCPソケットを作る
	if (fd < 0) {
		fatal_error();
	}
	/* sockaddr_in は IPv4 の住所（man 7 ip）。
	 * htonl/htons は数値を通信で使うバイト順へ変換する。 */
	bzero(&address, sizeof(address));	// 住所構造体を0で初期化
	address.sin_family = AF_INET;	// IPv4を使う
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);	// 127.0.0.1だけで待つ
	address.sin_port = htons(port);	// ポートを通信用バイト順へ変換
	// bind は fd に住所を登録する。引数型に合わせ sockaddr* へ変換する。
	if (bind(fd, (struct sockaddr*)&address, sizeof(address)) < 0) {	// fdへIPアドレスとポートを登録
		fatal_error();
	}
	if (listen(fd, LISTEN_BACKLOG) < 0) {	// 接続を受け付けられる状態へ変更
		fatal_error();
	}
	return (fd);	// 完成した受付用fdを返す
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
		read_fds = server->active_fds;	// select用に元の名簿をコピー
		FD_ZERO(&write_fds);	// 書き込み監視名簿を毎回作り直す
		fd = 0;	// fdを0番から順番に調べる
		while (fd <= server->max_fd) {
			if (fd != server->listen_fd
				&& FD_ISSET(fd, &server->active_fds)
				&& server->clients[fd].output) {
				FD_SET(fd, &write_fds);	// 送信待ちがあるfdだけ監視
			}
			fd++;
		}
		if (select(server->max_fd + 1, &read_fds, &write_fds, NULL, NULL) < 0) {
			fatal_error();
		}
		// listen_fd が読めるのは、新しい接続が待っている合図。
		if (FD_ISSET(server->listen_fd, &read_fds)) {
			accept_client(server);	// 新しいクライアントを登録
		}
		fd = 0;	// 再びfdを0番から調べる
		while (fd <= server->max_fd) {
			if (fd != server->listen_fd && FD_ISSET(fd, &server->active_fds)) {
				if (FD_ISSET(fd, &read_fds)) {
					receive_client(server, fd);	// 届いたデータを読む
				}
				// receive_client で切断されていない場合だけ送信する。
				if (FD_ISSET(fd, &server->active_fds) && FD_ISSET(fd, &write_fds)) {
					send_client(server, fd);	// 送信待ちデータを送る
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

	//OSから、新しいクライアントと通信するためのFD番号を返してもらう
	fd = accept(server->listen_fd, NULL, NULL);	// 相手専用の新しいfdを受け取る
	if (fd < 0 || fd >= FD_SETSIZE) {
		if (fd >= 0) {
			close(fd);	// 管理できないfdを閉じる
		}
		fatal_error();
	}
	server->clients[fd].id = server->next_id++;	// IDを渡して次の値へ進める
	FD_SET(fd, &server->active_fds);	// 新しいclient fdも監視する
	if (fd > server->max_fd) {
		server->max_fd = fd;	// select用の最大fdを更新
	}
	sprintf(message, "server: client %d just arrived\n", server->clients[fd].id);
	broadcast(server, fd, message);	// 新人以外へ入室通知を積む
}

/* ************************************************************************** */
// recvした文字をstr_joinで貯め、extract_messageで完成行を取り出す。
static void
	receive_client(t_server* server, int fd)
{
	t_client* client;
	size_t	buffer_size;
	buffer_size = 5000;
	char buffer[buffer_size+1];
	char prefix_message[42];
	char *receive_message;
	int received;
	int extracted;

	client = &server->clients[fd];
	received = recv(fd, buffer, buffer_size, 0);
	if (received <= 0){
		disconnect_client(server, fd);
		return ;
	}
	buffer[received] = '\0';
	client->input = str_join(client->input, buffer);
	if (client->input == 0){
		fatal_error();
	}
	extracted = extract_message(&client->input, &receive_message);
	while(extracted == 1)
	{
		sprintf(prefix_message, "client %d: ", client->id);
		broadcast(server, fd, prefix_message);
		broadcast(server, fd, receive_message);
		free(receive_message);
		extracted = extract_message(&client->input, &receive_message);
	}
	if (extract_message < 0)
	{
		fatal_error();
	}

//ssize_t recv(int sockfd, void *buf, size_t len, int flags);
}

/* ************************************************************************** */
// outputを送れる分だけ送り、未送信部分をstr_joinで複製する。
static void
	send_client(t_server* server, int fd)
{

}

/* ************************************************************************** */
// 切断した fd を名簿から外し、メモリと fd を解放する。
static void
	disconnect_client(t_server* server, int fd)
{

}

/* ************************************************************************** */
// except_fd 以外の全クライアントへ送信待ち文字列を追加する。
static void
	broadcast(t_server* server, int except_fd, const char* text)
{
	int	fd;

	fd = 0;	// 全fdを先頭から確認
	while (fd <= server->max_fd) {
		if (fd != server->listen_fd && fd != except_fd && FD_ISSET(fd, &server->active_fds)) {
			server->clients[fd].output = str_join(server->clients[fd].output, text);	// 配布関数で送信待ちへ追加
			if (!server->clients[fd].output) {
				fatal_error();	// str_joinのmalloc失敗
			}
		}
		fd++;
	}
}

/* ************************************************************************** */
// 配布main.cの関数。bufから改行までをmessageへ切り出す。
static int
	extract_message(char** buf, char** message)
{
	char*	newbuf;
	int		i;

	*message = NULL;	// まだ取り出したメッセージはない
	if (*buf == NULL) {
		return (0);	// 受信文字列自体がない
	}
	i = 0;
	while ((*buf)[i]) {
		if ((*buf)[i] == '\n') {
			newbuf = calloc(1, strlen(*buf + i + 1) + 1);	// 改行後の保存領域
			if (!newbuf) {
				return (-1);	// 呼び出し側でFatal errorにする
			}
			strcpy(newbuf, *buf + i + 1);	// 改行より後ろを保存
			*message = *buf;	// 古いbufの所有権をmessageへ渡す
			(*message)[i + 1] = '\0';	// 改行を含む1行で終端
			*buf = newbuf;	// 未完成部分を次回へ持ち越す
			return (1);	// 1行取り出せた
		}
		i++;
	}
	return (0);	// 改行がないので次のrecvを待つ
}

/* ************************************************************************** */
// 配布main.cの関数。bufの末尾へaddを連結し、古いbufを解放する。
static char*
	str_join(char* buf, const char* add)
{
	char*	newbuf;
	int		length;

	length = 0;	// bufがNULLなら既存長は0
	if (buf) {
		length = strlen(buf);	// 既存文字列の長さ
	}
	newbuf = malloc(length + strlen(add) + 1);	// 連結後の大きさを確保
	if (!newbuf) {
		return (NULL);	// 呼び出し側でFatal errorにする
	}
	newbuf[0] = '\0';	// strcatできる空文字列にする
	if (buf) {
		strcat(newbuf, buf);	// 既存文字列をコピー
	}
	free(buf);	// 古い領域は不要
	strcat(newbuf, add);	// 追加文字列を末尾へ連結
	return (newbuf);
}

/* ************************************************************************** */
// 致命的エラーを標準エラーへ出して終了する。
static void
	fatal_error(void)
{
	write(2, "Fatal error\n", 12);	// 標準エラーへ固定文言を出す
	exit(1);	// ステータス1で即終了
}
