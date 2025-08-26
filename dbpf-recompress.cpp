#include "dbpf.h"
#include "boost/nowide/args.hpp"
#include "boost/nowide/fstream.hpp"
#include "boost/nowide/iostream.hpp"

#include <filesystem>
#include <iomanip>
#include <string>
#include <vector>

namespace filesystem = std::filesystem;

using std::vector, std::string;
using std::endl, std::setprecision, std::fixed;
using boost::nowide::cout;
using boost::nowide::fstream;

bool validatePackage(dbpf::Package& oldPackage, dbpf::Package& newPackage, fstream& oldFile, fstream& newFile, string displayPath, dbpf::Mode mode);

//trys to delete a file, fails silently
void tryDelete(string fileName) {
	try { filesystem::remove(fileName); }
	catch(filesystem::filesystem_error) {}
}

int main(int argc, char *argv[]) {
    boost::nowide::args a(argc, argv); //fix arguments - make them UTF-8

	if(argc == 1) {
		cout << "No arguments provided" << endl;
		return 0;
	}

	//parse args
	string arg = argv[1];

	if(arg == "-h" || arg == "-help") {
		cout << "dbpf-recompress -args package_file_or_folder" << endl;
		cout << "  -d  decompress" << endl;
		cout << endl;
		return 0;
	}

	dbpf::Mode default_mode = dbpf::RECOMPRESS;
	int fileArgIndex = 1;

	if(arg == "-d") {
		default_mode = dbpf::DECOMPRESS;
		fileArgIndex = 2;
	}

	if(fileArgIndex > argc - 1) {
		cout << "No file path provided" << endl;
		return 0;
	}

	string pathName = argv[fileArgIndex];

	auto files = vector<filesystem::directory_entry>();
	bool is_dir = false;

	if(filesystem::is_regular_file(pathName)) {
		auto file_entry = filesystem::directory_entry(pathName);
		if(file_entry.path().extension() != ".package") {
			cout << "Not a package file" << endl;
			return 0;
		}

		files.push_back(file_entry);

	} else if(filesystem::is_directory(pathName)) {
		is_dir = true;
		for(auto& dir_entry: filesystem::recursive_directory_iterator(pathName)) {
			if(dir_entry.is_regular_file() && dir_entry.path().extension() == ".package") {
				files.push_back(dir_entry);
			}
		}

	} else {
		cout << "File not found" << endl;
		return 0;
	}

	for(auto& dir_entry: files) {
		auto mode = default_mode;

		//open file
		string fileName = dir_entry.path().string();
		string tempFileName = fileName + ".new";

		float current_size = dir_entry.file_size() / 1024.0;

		string displayPath; //for cout

		if(is_dir) {
			displayPath = filesystem::relative(fileName, pathName).string();
		} else {
			displayPath = fileName;
		}

		fstream file = fstream(fileName, std::ios::in | std::ios::binary);

		if(!file.is_open()) {
			cout << displayPath << ": Failed to open file" << endl;
			continue;
		}

		//get package
		dbpf::Package package = dbpf::getPackage(file, displayPath, mode);
		dbpf::Package oldPackage = package; //copy

		//optimization: if the package file has the compressor's signature then skip it
		if(mode == dbpf::RECOMPRESS && package.signature_in_package) {
			mode = dbpf::SKIP;
			file.close();
		}

		//error unpacking package, getPackage already prints an error so there is no need to print one here
		if(!package.unpacked) {
			file.close();
			continue;
		}

		//optimization: for DECOMPRESS mode skip the package file if all of it's entries are decompressed
		if(mode == dbpf::DECOMPRESS) {
			bool all_entries_decompressed = true;

			for(auto& entry: package.entries) {
				if(entry.compressed) {
					all_entries_decompressed = false;
					break;
				}
			}

			if(all_entries_decompressed) {
				mode = dbpf::SKIP;
				file.close();
			}
		}

		if(mode != dbpf::SKIP) {
			//compress entries, pack package, and write to temp file
			fstream tempFile = fstream(tempFileName, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);

			if(tempFile.is_open()) {
				dbpf::putPackage(tempFile, file, package, mode);

			} else {
				cout << displayPath << ": Failed to create temp file" << endl;
				file.close();
				continue;
			}

			//validate new file
			tempFile.seekg(0, std::ios::beg);
			dbpf::Package newPackage = dbpf::getPackage(tempFile, tempFileName, mode);
			bool is_valid = validatePackage(oldPackage, newPackage, file, tempFile, displayPath, mode);

			file.close();
			tempFile.close();

			if(!is_valid) {
				tryDelete(tempFileName);
				continue;
			}

			float new_size = filesystem::file_size(tempFileName) / 1024.0;

			//overwrite old file
			try {
				filesystem::rename(tempFileName, fileName);
			}

			catch(filesystem::filesystem_error) {
				cout << displayPath << ": Failed to overwrite file" << endl;
				tryDelete(tempFileName);
				continue;
			}
		}

		float new_size = filesystem::file_size(fileName) / 1024.0;

		//output file size to console
		cout << displayPath << " " << fixed << setprecision(2);

		if(current_size >= 1000) {
			cout << current_size / 1024.0 << " MB";
		} else {
			cout << current_size << " KB";
		}

		cout << " -> ";

		if(new_size >= 1000) {
			cout << new_size / 1024.0 << " MB";
		} else {
			cout << new_size << " KB";
		}

		cout << endl;
	}

	cout << endl;
	return 0;
}

//checks if the new package file is valid
bool validatePackage(dbpf::Package& oldPackage, dbpf::Package& newPackage, fstream& oldFile, fstream& newFile, string displayPath, dbpf::Mode mode) {
	//package unpacking failed, getPackage already prints an error
	if(!newPackage.unpacked) {
		return false;
	}

	//compare headers
	bytes oldHeader = dbpf::readFile(oldFile, 0, 96);
	bytes newHeader = dbpf::readFile(newFile, 0, 96);

	if(bytes(oldHeader.begin(), oldHeader.begin() + 36) != bytes(newHeader.begin(), newHeader.begin() + 36)
	|| bytes(oldHeader.begin() + 60, oldHeader.end()) != bytes(newHeader.begin() + 60, newHeader.end())) {
		cout << displayPath << ": New header does not match the old header" << endl;
		return false;
	}

	if(mode == dbpf::RECOMPRESS) {
		//should only have one hole for the compressor signature
		if(newPackage.header.holeIndexEntryCount != 1) {
			cout << displayPath << ": Wrong hole index count" << endl;
			return false;
		}

		//one hole index entry is 8 bytes long
		if(newPackage.header.holeIndexSize != 8) {
			cout << displayPath << ": Wrong hole index size" << endl;
			return false;
		}

		dbpf::Hole hole = newPackage.holes[0];

		//compressor signature is 8 bytes long
		if(hole.size != 8) {
			cout << ": Wrong hole size" << endl;
			return false;
		}

		bytes holeData = dbpf::readFile(newFile, hole.location, 8);
		uint pos = 0;

		uint sig = dbpf::getInt(holeData, pos);

		//if the file was compressed then the signature should be "BRG5"
		if(sig != dbpf::SIGNATURE) {
			cout << displayPath << ": Compressor signature not found" << endl;
			return false;
		}

		uint fileSizeInHole = dbpf::getInt(holeData, pos);
		uint fileSize = dbpf::getFileSize(newFile);

		//file size written in the hole should match the actual file size
		if(fileSizeInHole != fileSize) {
			cout << displayPath << ": File size in signature does not match the actual file size" << endl;
			return false;
		}
	}

	//should have the exact number of entries as the original package
	//NOTE: getPackage does not include the directory of compressed files entry in the entries vector for both packages
	if(oldPackage.entries.size() != newPackage.entries.size()) {
		cout << displayPath << ": Number of entries between old package and new package not matching" << endl;
		return false;
	}

	//compare entries
	for(uint i = 0; i < oldPackage.entries.size(); i++) {
		auto& oldEntry = oldPackage.entries[i];
		auto& newEntry = newPackage.entries[i];

		//compare TGIRs
		if(oldEntry.type != newEntry.type || oldEntry.group != newEntry.group || oldEntry.instance != newEntry.instance || oldEntry.resource != newEntry.resource) {
			cout << displayPath << ": Types, groups, instances, or resources of entries not matching" << endl;
			return false;
		}

		//check entry content
		bytes oldContent = dbpf::readFile(oldFile, oldEntry.location, oldEntry.size);
		bytes newContent = dbpf::readFile(newFile, newEntry.location, newEntry.size);

		//compression info in the directory of compressed files should match the information in the compression header
		bool compressed_in_header = newContent.size() >= 9 && newContent[4] == 0x10 && newContent[5] == 0xFB;
		auto iter = newPackage.compressedEntries.find(dbpf::CompressedEntry{newEntry.type, newEntry.group, newEntry.instance, newEntry.resource});
		bool in_clst = iter != newPackage.compressedEntries.end();

		if(compressed_in_header != in_clst) {
			cout << displayPath << ": Incorrect compression information" << endl;
			return false;
		}

		if(newEntry.compressed) {
			uint tempPos = 0;
			uint uncompressedSize = dbpf::getUncompressedSize(newContent);
			uint compressedSize = dbpf::getInt(newContent, tempPos);

			if(uncompressedSize != iter->uncompressedSize) {
				cout << displayPath << ": Mismatch between the uncompressed size in the compression header and the uncompressed size in the CLST" << endl;
				return false;
			}

			if(compressedSize != newEntry.size) {
				cout << displayPath << ": Mismatch between the compressed size in the compression header and the compressed size in the index" << endl;
				return false;
			}

			//the compressor should only produce compressed entries that are smaller than the original decompressed entries
			if(compressedSize > uncompressedSize) {
				cout << displayPath << ": Compressed size is larger than the uncompressed size for one entry" << endl;
				return false;
			}
		}

		//decompress the entries and compare them
		oldContent = dbpf::decompressEntry(oldEntry, oldContent);
		newContent = dbpf::decompressEntry(newEntry, newContent);

		if(oldContent != newContent) {
			cout << displayPath << ": Mismatch between old entry and new entry" << endl;
			return false;
		}
	}

	//if all passes then return true
	return true;
}