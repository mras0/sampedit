#ifndef SAMPEDIT_XM_H
#define SAMPEDIT_XM_H

#include <iosfwd>

struct module;

bool is_xm(std::istream& in);
void load_xm(std::istream& in, const char* filename, module& mod);

#endif