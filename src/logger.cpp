#include "logger.h"

void INFO(const std::string &msg)
{
    std::cout << "INFO" << msg << std::endl;
}

void ERROR(const std::string &msg)
{
    std::cout << "ERROR" << msg << std::endl;
}