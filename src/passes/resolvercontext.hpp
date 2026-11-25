/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef RESOLVERCONTEXT_HPP
#define RESOLVERCONTEXT_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "stride/parser/ast.h"
#include "stride/parser/langerror.h"
#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/stridegen/platformpolicymanager.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/stridegen/systemconfiguration.hpp"
#include "stride/utils/featureresolver.h"

namespace strd {

struct ResolverContext {
  ASTNode tree;
  std::shared_ptr<StrideSystem> system;
  SystemConfiguration systemConfig;
  std::string striderootDir;
  PlatformPolicyManager policyManager;
  FeatureConfig featureConfig;
  std::vector<LangError> errors;
  bool hardwareResolved = false;
  std::optional<ResolvedHardware> resolvedHardware;
  std::string hardwareResolveError;

  void addError(LangError::ErrorType type, const std::string &file, int line,
                const std::vector<std::string> &tokens) {
    LangError err;
    err.type = type;
    err.filename = file;
    err.lineNumber = line;
    err.errorTokens = tokens;
    errors.push_back(std::move(err));
  }
};

} // namespace strd

#endif // RESOLVERCONTEXT_HPP
