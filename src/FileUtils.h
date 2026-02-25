#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>

class FileUtils
{
public:
    // Read binary file into a char vector (used for SPIR-V shaders)
    static std::vector<char> readBinaryFile(const std::string& filename)
    {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        size_t fileSize = static_cast<size_t>(file.tellg());
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
        file.close();

        return buffer;
    }
};
