#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <string>
#include <map>

class Database {
private:
    std::map<std::string, std::string> _data;

public:
    // Server.cppからのデフォルト呼び出しに対応
    Database();
    ~Database();

    // Server.cppの呼び出し形式に合わせたメソッド群
    void load(const std::string& filepath);
    void save(const std::string& filepath);
    std::string processQuery(const std::string& request);
};

#endif