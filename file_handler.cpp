/* Author: Zoya Samsonov
 * Date: October 2, 2020
 */

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <vector>

std::vector<std::ofstream> outfiles;
std::vector<std::ifstream> infiles;

int add_outfile(const char* name) {
	outfiles.push_back(std::ofstream(name));
	return outfiles.size() - 1;
}

int add_outfile_append(const char* name) {
	outfiles.push_back(std::ofstream(name, std::ios_base::app));
	return outfiles.size() - 1;
}

int add_infile(const char* name) {
	infiles.push_back(std::ifstream(name));
	return infiles.size() - 1;
}

int readline(int filenum, std::string& outstr) {
	if (std::getline(infiles[filenum], outstr))	return 1;
	return 0;
}

void writeline(int filenum, std::string line) {
	outfiles[filenum] << line << "\n";
}

void close_outfile(int filenum) {
	outfiles[filenum].close();
}

void close_infile(int filenum) {
	infiles[filenum].close();
}

void close_all() {
	for (int i = 0; i < outfiles.size(); i++) {
		outfiles[i].close();
	}
	for (int i = 0; i < infiles.size(); i++) {
		infiles[i].close();
	}
}
