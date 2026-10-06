#include "filesystem_domain.h"

#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <thread>

namespace {

namespace fs = std::filesystem;

void Require(bool condition, const char *message) {
	if (!condition) {
		throw std::runtime_error(message);
	}
}

Export::Settings SettingsFor(const fs::path &parent, bool singlePeer = true) {
	auto settings = Export::Settings();
	settings.path = parent.string();
	settings.singlePeer = singlePeer;
	return settings;
}

std::string ExportBase(bool singlePeer = true) {
	return std::string(singlePeer ? "ChatExport_" : "DataExport_")
		+ QDate::currentDate().toString(Qt::ISODate).str();
}

void Write(const fs::path &path, const std::string &text) {
	auto file = std::ofstream(path, std::ios::binary);
	file << text;
	Require(bool(file), "fixture file write must succeed");
}

std::string Read(const fs::path &path) {
	auto file = std::ifstream(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

void ConcurrentReservations(const fs::path &root) {
	const auto parent = root / "concurrent";
	const auto sibling = parent / "preserved";
	fs::create_directories(sibling);
	Write(sibling / "data.bin", "original sibling payload");
	const auto settings = SettingsFor(parent);
	auto start = std::atomic<bool>(false);
	auto outputs = std::vector<std::optional<QString>>(32);
	auto threads = std::vector<std::thread>();
	for (auto i = std::size_t(0); i != outputs.size(); ++i) {
		threads.emplace_back([&, i] {
			while (!start.load(std::memory_order_acquire)) {
				std::this_thread::yield();
			}
			outputs[i] = Export::Output::NormalizePath(settings);
		});
	}
	start.store(true, std::memory_order_release);
	for (auto &thread : threads) {
		thread.join();
	}
	auto unique = std::set<std::string>();
	for (const auto &output : outputs) {
		Require(output.has_value(), "all concurrent reservations must succeed");
		Require(output->endsWith('/'), "reserved path must keep trailing slash");
		Require(fs::is_directory(output->str()), "returned reservation must already exist");
		Require(unique.insert(output->str()).second, "each job must reserve a unique folder");
	}
	Require(unique.size() == 32, "32 jobs must produce 32 distinct reserved folders");
	Require(Read(sibling / "data.bin") == "original sibling payload", "sibling payload must remain unchanged");
}

void FileDirectoryAndDanglingLinkCollisions(const fs::path &root) {
	const auto parent = root / "collisions";
	fs::create_directories(parent);
	const auto base = ExportBase();
	Write(parent / base, "existing candidate file");
	fs::create_symlink(parent / "missing-target", parent / (base + " (1)"));
	fs::create_directory(parent / (base + " (2)"));
	Write(parent / (base + " (2)") / "data.bin", "existing candidate folder payload");
	const auto output = Export::Output::NormalizePath(SettingsFor(parent));
	Require(output.has_value(), "collision search must find the next folder");
	Require(fs::path(output->str()).lexically_normal().filename() == "", "output path must include trailing slash");
	Require(fs::path(output->str()).parent_path().filename() == base + " (3)", "file, dangling link and directory must each be skipped");
	Require(Read(parent / base) == "existing candidate file", "existing file must stay intact");
	Require(fs::is_symlink(fs::symlink_status(parent / (base + " (1)"))), "dangling link must stay intact");
	Require(fs::read_symlink(parent / (base + " (1)")) == parent / "missing-target", "dangling link target must stay intact");
	Require(Read(parent / (base + " (2)") / "data.bin") == "existing candidate folder payload", "existing folder data must stay intact");
}

void PermanentCreationFailure(const fs::path &root) {
	const auto blocked = root / "parent-is-file";
	Write(blocked, "parent blocker");
	for (auto i = 0; i != 32; ++i) {
		Require(!Export::Output::NormalizePath(SettingsFor(blocked)), "invalid parent must return no reservation");
	}
	Require(Read(blocked) == "parent blocker", "failed reservation must preserve parent blocker");
	const auto missingChild = blocked / "child";
	Require(!Export::Output::NormalizePath(SettingsFor(missingChild)), "uncreatable nested parent must return no reservation");
}

void CandidateCreationFailure(const fs::path &root) {
	const auto parent = root / "candidate-is-too-long";
	fs::create_directories(parent);
	const auto settings = SettingsFor(parent);
	const auto before = fs::directory_iterator(parent);
	Require(before == fs::directory_iterator(), "failure fixture must start empty");
	TestFilesystem::SetDateOverride(QString(std::string(1024, 'x')));
	const auto output = Export::Output::NormalizePath(settings);
	TestFilesystem::SetDateOverride(std::nullopt);
	Require(!output, "failed candidate mkdir must return no unreserved path");
	Require(fs::directory_iterator(parent) == fs::directory_iterator(), "failed mkdir must leave parent unchanged");
}

void MissingParentAndAccountExport(const fs::path &root) {
	const auto parent = root / "missing" / "nested";
	const auto output = Export::Output::NormalizePath(SettingsFor(parent, false));
	Require(output.has_value(), "missing parent must be created when possible");
	Require(fs::is_directory(output->str()), "account export folder must be reserved");
	Require(fs::path(output->str()).parent_path().filename() == ExportBase(false), "account export must use DataExport prefix");
}

}

int main(int argc, char *argv[]) {
	Require(argc == 2, "runner must provide an owned temporary fixture path");
	const auto root = fs::path(argv[1]);
	fs::create_directory(root);
	const auto tests = std::vector<std::pair<const char*, std::function<void(const fs::path&)>>>{
		{ "32_concurrent_reservations", ConcurrentReservations },
		{ "file_directory_dangling_link_collisions", FileDirectoryAndDanglingLinkCollisions },
		{ "permanent_parent_creation_failure", PermanentCreationFailure },
		{ "candidate_mkdir_failure", CandidateCreationFailure },
		{ "missing_parent_and_account_export", MissingParentAndAccountExport },
	};
	auto failed = 0;
	for (const auto &[name, run] : tests) {
		try {
			run(root);
			std::cout << "PASS " << name << '\n';
		} catch (const std::exception &error) {
			++failed;
			std::cerr << "FAIL " << name << ": " << error.what() << '\n';
		}
	}
	std::cout << (tests.size() - failed) << '/' << tests.size() << " output tests passed\n";
	return failed ? 1 : 0;
}
