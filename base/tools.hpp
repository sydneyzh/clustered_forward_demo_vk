#pragma once
#include <string>
#include <fstream>

namespace base
{
bool file_exists(const std::string& full_path)
{
    std::ifstream f(full_path.c_str());
    return !f.fail();
}

bool ends_width(std::string& str, std::string ending)
{
    if (ending.size() > str.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), str.rbegin());
}
} // namespace base