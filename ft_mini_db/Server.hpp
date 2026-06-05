// Server.hpp
#ifndef SERVER_HPP
#define SERVER_HPP

#include "Database.hpp"
#include "Socket.hpp"
#include <string>
#include <sys/select.h>

// 【単一責任原則】ソケットとデータベースを仲介し、select() によるI/O多重化ループを管理する
class Server {
 public:
  Server();
  ~Server();

  void init(int port, const std::string& db_path);
  void run();
  void stop();
  void saveDatabase() const;

 private:
  Socket listening_socket_;
  Database _db;
  std::string _db_path;
  fd_set _active_fds; // 監視対象のFDビットマップ群
  int _max_fd;        // selectの走査範囲を最適化するための最大FD値
  bool _is_running;

  void handleConnections();
  void handleNewConnection();
  void handleClientData(int fd);
  void removeClient(int fd);

  Server(const Server&);
  Server& operator=(const Server&);
};

#endif  // SERVER_HPP