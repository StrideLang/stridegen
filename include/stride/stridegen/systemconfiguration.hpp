/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Based on BSD/MIT code by Andres Cabrera and Joseph Tilbian.

    Author: Andres Cabrera
*/

#ifndef SYSTEMCONFIGURATION_HPP
#define SYSTEMCONFIGURATION_HPP

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/valuenode.h"

#include <map>
#include <string>
#include <variant>

namespace strd {
using ConfigMap =
    std::map<std::string, std::variant<int64_t, double, std::string>>;

class SystemConfiguration {
public:
  SystemConfiguration();
  std::map<std::string, ConfigMap>
      overrides; // Override existing values in Stride Code
  std::map<std::string, ConfigMap>
      substitutions; // Substitute strings in generated code
  std::map<std::string, std::vector<std::shared_ptr<DeclarationNode>>>
      resourceConfigurations; // Configure resources
  std::map<std::string, ConfigMap>
      frameworkConfigurations; // Configure frameworks

  bool testing = false;

  void readConfiguration(std::string filename);
  void writeConfiguration(std::string filename);
};
} // namespace strd

#endif // SYSTEMCONFIGURATION_HPP
