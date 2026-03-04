#pragma once

#include <string>

#include "spqs/config.hpp"

namespace spqs {

Config default_config();
bool load_config(const std::string& yaml_path, Config* out, std::string* error);

}  // namespace spqs
