/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

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

#ifndef CODERESOLVER_H
#define CODERESOLVER_H

#include <memory>
#include <string>
#include <vector>

#include "stride/parser/ast.h"
#include "stride/parser/langerror.h"
#include "stride/stridegen/platformpolicymanager.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/stridegen/systemconfiguration.hpp"
#include "stride/utils/featureresolver.h"

namespace strd {

struct ResolverContext;

// Resolves a parsed Stride AST against a target system through a sequence of
// focused lowering and analysis passes.
class CodeResolver {
public:
  CodeResolver(ASTNode tree, std::string striderootDir,
               SystemConfiguration systemConfig = SystemConfiguration(),
               FeatureConfig featureConfig = FeatureConfig());
  CodeResolver(ASTNode tree, std::string striderootDir,
               FeatureConfig featureConfig)
      : CodeResolver(tree, striderootDir, SystemConfiguration(), featureConfig) {}
  ~CodeResolver();

  void process();

  // Errors found while resolving, such as a gpioPin the board can't support.
  const std::vector<LangError> &getErrors() const;
  std::shared_ptr<StrideSystem> getSystem() const;
  const PlatformPolicyManager &getPolicyManager() const;
  PlatformPolicyManager &getPolicyManager();
  const FeatureConfig &getFeatureConfig() const;

private:
  std::unique_ptr<ResolverContext> m_ctx;
};

} // namespace strd

#endif // CODERESOLVER_H
