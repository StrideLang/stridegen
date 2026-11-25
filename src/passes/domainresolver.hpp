/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef DOMAINRESOLVER_HPP
#define DOMAINRESOLVER_HPP

#include "resolvercontext.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/streamnode.h"
#include "stride/utils/astquery.h"

#include <vector>

namespace strd {

class DomainResolver {
public:
  explicit DomainResolver(ResolverContext &ctx) : m_ctx(ctx) {}

  void resolveDomainAliases();
  void processDomains();

private:
  void propagateDomainsForNode(const ASTNode &node, ScopeStack scopeStack,
                               const ASTNode &defaultDomain);
  void resolveDomainsForStream(std::shared_ptr<StreamNode> stream,
                               const ScopeStack &scopeStack,
                               const ASTNode &contextDomainNode);
  ASTNode processDomainsForNode(const ASTNode &node,
                                const ScopeStack &scopeStack,
                                std::vector<ASTNode> &domainStack);
  void setDomainForStack(const std::vector<ASTNode> &domainStack,
                         const ASTNode &domainName,
                         const ScopeStack &scopeStack);
  void setDomainForNode(const ASTNode &node, const ASTNode &domain,
                        const ScopeStack &scopeStack, bool force = false);

  std::shared_ptr<StreamNode> makeStreamFromStack(std::vector<ASTNode> &stack);
  std::vector<ASTNode> sliceStreamByDomain(const std::shared_ptr<StreamNode> &stream,
                                           const ScopeStack &scopeStack);
  void sliceDomainsInNode(const std::shared_ptr<DeclarationNode> &module,
                          ScopeStack scopeStack);

  ASTNode getModuleContextDomain(const std::shared_ptr<DeclarationNode> &moduleDecl);
  ASTNode getDomainOfUsedGlobals(const std::vector<ASTNode> &streams);
  ASTNode applyDomainOfUsedGlobals(const std::shared_ptr<DeclarationNode> &module,
                                   const std::vector<ASTNode> &streams);

  ResolverContext &m_ctx;
};

} // namespace strd

#endif // DOMAINRESOLVER_HPP
