// Socket.hpp
#ifndef SOCKET_HPP
#define SOCKET_HPP

#include <netinet/in.h>

// 【単一責任原則】OSのネットワークシステムコールの隠蔽と、ファイルディスクリプタ(FD)のライフサイクル管理
class Socket {
 public:
  Socket();
  ~Socket();

  void init(int port);
  int acceptConnection() const;
  int getFd() const;

 private:
  int _sockfd;
  struct sockaddr_in _address;

  // 【安全性向上】誤ったコピーによる同一FDの二重close(Double Free問題に類似)を防ぐコピーガード
  Socket(const Socket&);
  Socket& operator=(const Socket&);
};

#endif  // SOCKET_HPP