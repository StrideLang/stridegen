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

    Author: Andres Cabrera
*/

#ifndef STRIDE_PLUGINS_PLUGINLOADER_HPP
#define STRIDE_PLUGINS_PLUGINLOADER_HPP

#include <memory>
#include <string>
#include <vector>

#include "stride/parser/ast.h"
#include "stride/stridegen/plugins/builder.hpp"

namespace strd {
class StrideSystem;

// Owns a Builder instantiated from a dynamically loaded platform plugin, keeping
// the underlying shared library loaded until the Builder is destroyed.
struct LoadedPluginBuilder {
  std::shared_ptr<void> libraryHandle;
  std::unique_ptr<Builder> builder;
};

class PluginLoader {
public:
  static std::string ensureProductsDirectory(const std::string &fileName);

  static std::vector<LoadedPluginBuilder>
  loadBuilders(StrideSystem &system, const std::string &fileName,
               const ASTNode &tree);
};
} // namespace strd

#endif // STRIDE_PLUGINS_PLUGINLOADER_HPP
