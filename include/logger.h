#pragma one
#include <iostream>
#include <string>
#include <cstring> //支持strerror()转换错误码
#include <cerrno>  //全局错误码

void INFO(const std::string &msg);
void ERROR(const std::string &msg);