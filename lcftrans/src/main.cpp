/*
 * Copyright (c) 2020 LcfTrans authors
 * This file is released under the MIT License
 * http://opensource.org/licenses/MIT
 */

#include <cstring>
#include <fstream>
#include <iostream>
#include <lcf/encoder.h>
#include <lcf/reader_util.h>
#include <lcf/ldb/reader.h>
#include <argparse.hpp>

#include "translation.h"
#include "utils.h"

#ifdef _WIN32
#  include <dirent_win.h>
#else
#  include <dirent.h>
#endif

#define DATABASE_FILE "rpg_rt.ldb"
#define MAPTREE_FILE "rpg_rt.lmt"
#define INI_FILE "rpg_rt.ini"

void DumpLdb(const std::string& filename);
void DumpLmu(const std::string& filename);
void DumpLmt(const std::string& filename);
int MatchMode();

namespace {
	/* config */
	std::string encoding, outdir = ".", merge_indir, indir;
	std::vector<std::pair<std::string, std::string>> source_files;
	std::vector<std::pair<std::string, std::string>> outdir_files;
	std::string ini_file, database_file;
	bool create, update, match = false;
}

int main(int argc, char** argv) {
	/* add usage and help messages */
	argparse::ArgumentParser cli("lcftrans", PACKAGE_VERSION);
	cli.set_usage_max_line_width(100);
	cli.add_description("Translate RPG Maker 2000/2003 projects");
	cli.add_epilog("Homepage " PACKAGE_URL " - Report bugs at: " PACKAGE_BUGREPORT);

	/* parse command line arguments */
	cli.add_argument("DIRECTORY").store_into(indir).metavar("DIRECTORY")
		.help("Game directory");

	auto &group = cli.add_mutually_exclusive_group(true);
	group.add_argument("-c", "--create").store_into(create)
		.help("Create a new translation");
	group.add_argument("-u", "--update").store_into(update)
		.help("Update an existing translation");
	group.add_argument("-m", "--match").metavar("MDIR")
		.action([&](const auto &arg) { merge_indir = arg; match = true; })
		.help("Match the translations in MDIR and DIRECTORY. When matched\n"
			"the original in MDIR becomes the translation of DIRECTORY.\n"
			"Used to generate translations from games where the trans-\n"
			"lation is hardcoded in the game files.");

	cli.add_argument("-e", "--encoding").store_into(encoding).metavar("ENC")
		.help("When not specified, is read from RPG_RT.ini or auto-detected");
	cli.add_argument("-o", "--output").store_into(outdir).metavar("OUTDIR")
		.help("Output directory (default: working directory)");
	// for old encoding argument
	cli.add_argument("additional").remaining().hidden();

	try {
		cli.parse_args(argc, argv);
	} catch (const std::exception& err) {
		std::cerr << err.what() << "\n";
		// print usage message
		std::cerr << cli.usage() << "\n";
		std::exit(EXIT_FAILURE);
	}

	// for old encoding argument
	if (auto additional_args = cli.present<std::vector<std::string>>("additional")) {
		if(additional_args.value().size() > 1) {
			std::cerr << "Found additional, unrecognized arguments.\n";
			std::cerr << cli.usage() << "\n";
			std::exit(EXIT_FAILURE);
		} else {
			std::cerr << "Specifying ENCODING as last argument is deprecated, `-e ENC` is the replacement.\n";
			encoding = additional_args.value()[0];
		}
	}

	if (outdir == merge_indir) {
		std::cerr << "You need to specify a different output directory (-o).\n";
		std::cerr << cli;
		std::exit(EXIT_FAILURE);
	}

	auto full_path = [&](const auto& name) {
		return indir + "/" + name;
	};

	DIR *dirHandle;
	struct dirent* dirEntry;

	dirHandle = opendir(outdir.c_str());
	if (!dirHandle) {
		std::cerr << "Cannot access output directory " << outdir << "\n";
		return 1;
	}
	if (update) {
		while (nullptr != (dirEntry = readdir(dirHandle))) {
			outdir_files.emplace_back(dirEntry->d_name, Utils::LowerCase(dirEntry->d_name));
		}
	} else if (match) {
		// Also read the source translations
		closedir(dirHandle);
		dirHandle = opendir(indir.c_str());
		if (!dirHandle) {
			std::cerr << "Cannot access input directory " << indir << "\n";
			return 1;
		}
		while (nullptr != (dirEntry = readdir(dirHandle))) {
			outdir_files.emplace_back(dirEntry->d_name, Utils::LowerCase(dirEntry->d_name));
		}
		closedir(dirHandle);
		dirHandle = opendir(merge_indir.c_str());
		if (!dirHandle) {
			std::cerr << "Cannot access merge input directory " << merge_indir << "\n";
			return 1;
		}
		while (nullptr != (dirEntry = readdir(dirHandle))) {
			source_files.emplace_back(dirEntry->d_name, Utils::LowerCase(dirEntry->d_name));
		}
	}

	closedir(dirHandle);

	if (match) {
		return MatchMode();
	}

	dirHandle = opendir(indir.c_str());
	if (dirHandle) {
		while (nullptr != (dirEntry = readdir(dirHandle))) {
			const auto& lname = Utils::LowerCase(dirEntry->d_name);
			source_files.emplace_back(dirEntry->d_name, lname);

			if (lname == INI_FILE) {
				ini_file = full_path(dirEntry->d_name);
			} else if (lname == DATABASE_FILE) {
				database_file = full_path(dirEntry->d_name);
			}
		}
		closedir(dirHandle);
	} else {
		std::cerr << "Failed reading dir " << indir << "\n";
		return 1;
	}

	if (encoding.empty()) {
		if (!ini_file.empty()) {
			encoding = lcf::ReaderUtil::GetEncoding(ini_file);
		}
		if (encoding.empty() && !database_file.empty()) {
			auto db = lcf::LDB_Reader::Load(database_file, encoding);
			if (db) {
				encoding = lcf::ReaderUtil::DetectEncoding(*db);
			}
		}
	}

	lcf::Encoder enc(encoding);
	if (!enc.IsOk()) {
		std::cerr << "Bad encoding " << encoding << "\n";
		return 3;
	}
	std::cout << "LcfTrans\n";
	std::cout << "Using encoding " << encoding << "\n";

	std::sort(source_files.begin(), source_files.end(), [](const auto& a, const auto& b) {
		return a.first < b.first;
	});

	for (const auto& s : source_files) {
		const auto& name = s.first;
		const auto& lname = s.second;

		if (lname == DATABASE_FILE) {
			std::cout << "Parsing Database " << name << "\n";
			DumpLdb(full_path(name));
		} else if (lname == MAPTREE_FILE) {
			std::cout << "Parsing Maptree " << name << "\n";
			DumpLmt(full_path(name));
		} else if (Utils::HasExt(lname, ".lmu")) {
			std::cout << "Parsing Map " << name << "\n";
			DumpLmu(full_path(name));
		}
	}

	return 0;
}

static std::string get_outdir_file(const std::string& file) {
	for (const auto& f : outdir_files) {
		if (f.second == file) {
			return f.first;
		}
	}
	return "";
}

void DumpLdb(const std::string& filename) {
	TranslationLdb t = Translation::fromLDB(filename, encoding);

	auto dump = [](Translation& ti, const std::string& poname) {
		if (update) {
			std::string po = get_outdir_file(Utils::LowerCase(poname + ".po"));
			if (!po.empty()) {
				Translation pot;
				pot = Translation::fromPO(outdir + "/" + po);
				auto stale = ti.Merge(pot);
				if (!stale.getEntries().empty()) {
					std::string term = stale.getEntries().size() == 1 ? " term is " : " terms are ";

					std::cout << " " << stale.getEntries().size() << term << "stale\n";
					std::ofstream outfile(outdir + "/" + poname + ".stale.po");
					stale.write(outfile);
				}
			}
		}

		std::ofstream outfile(outdir + "/" + poname + ".po");
		ti.write(outfile);
	};

	auto term = [](const Translation& t) {
		return std::to_string(t.getEntries().size()) + " " + (t.getEntries().size() == 1 ? "term " : "terms ");
	};

	std::cout << " " << term(t.terms) << "in the database\n";
	dump(t.terms, "RPG_RT.ldb");

	std::cout << " " << term(t.common_events) << "in Common Events\n";
	dump(t.common_events, "RPG_RT.ldb.common");

	std::cout << " " << term(t.battle_events) << "in Battle Events\n";
	dump(t.battle_events, "RPG_RT.ldb.battle");
}

void DumpLmuLmtInner(const std::string& filename, Translation& t, const std::string& poname) {
	Translation pot;

	if (t.getEntries().empty()) {
		std::cout << " Skipped. No terms found.\n";
		return;
	}

	std::cout << " " << t.getEntries().size() << " term" << (t.getEntries().size() == 1 ? "" : "s") << "\n";

	if (update) {
		std::string po = get_outdir_file(Utils::LowerCase(poname + ".po"));
		if (!po.empty()) {
			pot = Translation::fromPO(outdir + "/" + po);
			auto stale = t.Merge(pot);
			if (!stale.getEntries().empty()) {
				std::string term = stale.getEntries().size() == 1 ? " term is " : " terms are ";
				std::cout << " " << stale.getEntries().size() << term << "stale\n";
				std::ofstream outfile(outdir + "/" + poname + ".stale.po");
				stale.write(outfile);
			}
		}
	}

	std::ofstream outfile(outdir + "/" + poname + ".po");

	t.write(outfile);
}

void DumpLmu(const std::string& filename) {
	Translation t = Translation::fromLMU(filename, encoding);
	DumpLmuLmtInner(filename, t, Utils::GetFilename(filename));
}

void DumpLmt(const std::string& filename) {
	Translation t = Translation::fromLMT(filename, encoding);
	DumpLmuLmtInner(filename, t, "RPG_RT.lmt");
}

int MatchMode() {
	std::sort(source_files.begin(), source_files.end(), [](const auto& a, const auto& b) {
		return a.first < b.first;
	});

	auto ends_with_po = [](const std::string& src) {
		std::string po = ".po";
		if (src.length() >= po.size()) {
			std::string src_l = Utils::LowerCase(src);
			return (0 == src_l.compare(src_l.length() - po.length(), po.length(), po));
		} else {
			return false;
		}
	};

	for (const auto& s : source_files) {
		if (!ends_with_po(s.first)) {
			continue;
		}

		for (const auto& o : outdir_files) {
			if (s.second == o.second) {
				Translation src_po = Translation::fromPO(merge_indir + "/" + s.first);
				Translation dst_po = Translation::fromPO( indir + "/" + o.first);
				int matched;
				auto stale = dst_po.Match(src_po, matched);
				std::cout << "Matching " << o.first << "\n";
				std::cout << " " << matched << " term" << (matched != 1 ? "s" : "") << " matched\n";

				int fuzzy = 0;
				for (const auto& e : dst_po.getEntries()) {
					if (e.fuzzy) {
						++fuzzy;
					}
				}
				if (fuzzy > 0) {
					std::string term = fuzzy == 1 ? " term is " : " terms are ";
					std::cout << " " << fuzzy << term << "fuzzy matched\n";
				}

				if (!stale.getEntries().empty()) {
					std::string term = stale.getEntries().size() == 1 ? " term is " : " terms are ";
					std::cout << " " << stale.getEntries().size() << term << "unmatched\n";
					std::ofstream outfile(outdir + "/" + o.first.substr(0, o.first.size() - 3) + ".unmatched.po");
					stale.write(outfile);
				}
				std::ofstream outfile(outdir + "/" + o.first);
				dst_po.write(outfile);
				continue;
			}
		}
	}

	return 0;
}
