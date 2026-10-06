#include "filesystem_domain.h"

namespace {

std::optional<QString> DateOverride;

}

QString::QString(const char *text) : _text(text) {
}

QString::QString(std::string text) : _text(std::move(text)) {
}

bool QString::endsWith(char value) const {
	return !_text.empty() && _text.back() == value;
}

QString QString::arg(const QString &value) const {
	auto result = _text;
	const auto index = result.find("%1");
	if (index != std::string::npos) {
		result.replace(index, 2, value.str());
	}
	return result;
}

const std::string &QString::str() const {
	return _text;
}

QString QString::number(int value) {
	return std::to_string(value);
}

QString operator+(const QString &first, const QString &second) {
	return first.str() + second.str();
}

QString operator+(const QString &first, char second) {
	return first.str() + second;
}

QString &QString::operator+=(const QString &other) {
	_text += other.str();
	return *this;
}

QString operator""_q(const char16_t *text, std::size_t size) {
	auto result = std::string();
	for (auto i = std::size_t(0); i != size; ++i) {
		if (text[i] > 127) {
			throw std::runtime_error("filesystem test literal must be ASCII");
		}
		result.push_back(static_cast<char>(text[i]));
	}
	return result;
}

QDate QDate::currentDate() {
	return {};
}

QString QDate::toString(Qt::DateFormat format) const {
	if (DateOverride) {
		return *DateOverride;
	}
	const auto date = std::chrono::year_month_day(
		std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
	auto stream = std::ostringstream();
	stream << int(date.year()) << '-'
		<< std::setw(2) << std::setfill('0') << unsigned(date.month()) << '-'
		<< std::setw(2) << std::setfill('0') << unsigned(date.day());
	return stream.str();
}

QDir::QDir(const QString &path) : _path(path.str()) {
}

QString QDir::absolutePath() const {
	return std::filesystem::absolute(_path).lexically_normal().string();
}

bool QDir::exists() const {
	auto error = std::error_code();
	return std::filesystem::is_directory(_path, error);
}

bool QDir::EntryList::isEmpty() const {
	return entries.empty();
}

QDir::EntryList QDir::entryInfoList(int mode) const {
	auto result = EntryList();
	auto error = std::error_code();
	auto iterator = std::filesystem::directory_iterator(_path, error);
	while (!error && iterator != std::filesystem::directory_iterator()) {
		result.entries.emplace_back(iterator->path().filename().string());
		iterator.increment(error);
	}
	return result;
}

bool QDir::mkpath(const QString &name) const {
	const auto path = _path / name.str();
	auto error = std::error_code();
	return std::filesystem::create_directories(path, error)
		|| (!error && std::filesystem::is_directory(path, error));
}

bool QDir::mkdir(const QString &name) const {
	auto error = std::error_code();
	return std::filesystem::create_directory(_path / name.str(), error);
}

QFileInfo::QFileInfo(const QString &path) : _path(path.str()) {
}

bool QFileInfo::exists() const {
	auto error = std::error_code();
	return std::filesystem::exists(_path, error);
}

bool QFileInfo::isSymLink() const {
	auto error = std::error_code();
	return std::filesystem::is_symlink(std::filesystem::symlink_status(_path, error));
}

void TestFilesystem::SetDateOverride(std::optional<QString> date) {
	DateOverride = std::move(date);
}

bool Export::Settings::onlySinglePeer() const {
	return singlePeer;
}
