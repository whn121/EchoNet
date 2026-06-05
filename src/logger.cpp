#include "logger.h"
#include <string>

void INFO(const std::string &msg)
{
    std::cout << "[INFO]" << msg << std::endl;
}

void ERROR(const std::string &msg)
{
    std::cout << "[ERROR]" << msg << "," << strerror(errno) << std::endl;
}