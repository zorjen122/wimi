#include "File.h"
#include <fstream>
#include <iostream>
#include <vector>

FileType ParseFileType(const std::string& filePath)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file!" << std::endl;
        return FileType::Unknown;
    }

    std::vector<char> header(4);
    file.read(header.data(), header.size());

    if (header[0] == (char)0x89 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G') {
        return FileType::PNG;
    } else if (header[0] == (char)0xFF && header[1] == (char)0xD8 && header[2] == (char)0xFF) {
        return FileType::JPEG;
    } else if (header[0] == '%' && header[1] == 'P' && header[2] == 'D' && header[3] == 'F') {
        return FileType::PDF;
    } else if (header[0] == 'P' && header[1] == 'K' && header[2] == 0x03 && header[3] == 0x04) {
        return FileType::ZIP;
    } else if (header[0] == (char)0x1F && header[1] == (char)0x8B) {
        return FileType::GZIP;
    }

    return FileType::Unknown;
}