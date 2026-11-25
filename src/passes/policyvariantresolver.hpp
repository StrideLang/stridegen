/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef POLICYVARIANTRESOLVER_HPP
#define POLICYVARIANTRESOLVER_HPP

#include "resolvercontext.hpp"
#include "stride/utils/astquery.h"

#include <map>
#include <memory>
#include <string>

namespace strd {

class PolicyVariantResolver {
public:
  explicit PolicyVariantResolver(ResolverContext &ctx) : m_ctx(ctx) {}

  void resolvePolicyVariants();

private:
  void resolveForNode(
      const ASTNode &node,
      const std::shared_ptr<DeclarationNode> &activeDomain, ScopeStack &scope,
      const std::map<std::string, std::shared_ptr<DeclarationNode>> &domains);

  ResolverContext &m_ctx;
};

} // namespace strd

#endif // POLICYVARIANTRESOLVER_HPP
