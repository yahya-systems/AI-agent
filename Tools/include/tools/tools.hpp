#pragma once
#include "pugixml/pugixml.hpp"
#include <string>

void load_tools();
std::string execute_tool(pugi::xml_node node);
std::string get_tool_instructions();
