#pragma once

#include <string>

// Directory containing the running executable (with trailing separator).
std::string executableDir();

// Reads a whole text file; returns false on failure.
bool readTextFile(const std::string& path, std::string& out);
