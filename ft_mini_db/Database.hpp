#ifndef DATABASE_HPP
#define DATABASE_HPP

/* ************************************************************************** */

#include <map>
#include <string>

/* ************************************************************************** */

// 【単一責任原則】データのオンメモリ保持、パース、ファイルへのシリアライズ/デシリアライズのみを担う
class Database {
 public:
	Database();
	~Database();

	bool		load(const std::string& path);
	bool		save(const std::string& path) const;
	std::string	processQuery(const std::string& query);

 private:
	// 内部実装は赤黒木(Red-Black Tree)。要素の検索・挿入・削除は常に O(log N) を保証。
	// 空間計算量は O(N) (Nはキーの数)。
	std::map<std::string, std::string>	_data;

	std::string	handlePost(const std::string& key, const std::string& value);
	std::string	handleGet(const std::string& key, const std::string& value);
	std::string	handleDelete(const std::string& key, const std::string& value);
};

#endif // DATABASE_HPP