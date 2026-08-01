#pragma once

#include <climits>
#include <string>

#define MAX_FILE_SIZE_512MB (512 * 1024 * 1024)

enum class FileType { PNG, JPEG, PDF, ZIP, GZIP, Unknown };

FileType ParseFileType(const std::string& filePath);