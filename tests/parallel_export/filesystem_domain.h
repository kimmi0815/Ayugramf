#pragma once

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

class QString {
public:
	QString() = default;
	QString(const char *text);
	QString(std::string text);
	bool endsWith(char value) const;
	QString arg(const QString &value) const;
	const std::string &str() const;
	static QString number(int value);
	friend QString operator+(const QString &first, const QString &second);
	friend QString operator+(const QString &first, char second);
	QString &operator+=(const QString &other);

private:
	std::string _text;

};

QString operator""_q(const char16_t *text, std::size_t size);

namespace Qt {

enum DateFormat {
	ISODate,
};

}

class QDate {
public:
	static QDate currentDate();
	QString toString(Qt::DateFormat format) const;

};

class QDir {
public:
	struct EntryList {
		std::vector<QString> entries;
		bool isEmpty() const;
	};

	enum Filter {
		AllEntries = 1,
		NoDotAndDotDot = 2,
	};

	explicit QDir(const QString &path);
	QString absolutePath() const;
	bool exists() const;
	EntryList entryInfoList(int mode) const;
	bool mkpath(const QString &name) const;
	bool mkdir(const QString &name) const;

private:
	std::filesystem::path _path;

};

class QFileInfo {
public:
	explicit QFileInfo(const QString &path);
	bool exists() const;
	bool isSymLink() const;

private:
	std::filesystem::path _path;

};

namespace TestFilesystem {

void SetDateOverride(std::optional<QString> date);

}

namespace Export {

struct Settings {
	QString path;
	bool forceSubPath = true;
	bool singlePeer = true;
	bool onlySinglePeer() const;
};

namespace Output {

std::optional<QString> NormalizePath(const Settings &settings);

}

}
