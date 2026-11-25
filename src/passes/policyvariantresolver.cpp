/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "policyvariantresolver.hpp"

#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"

#include <functional>

using namespace strd;

void PolicyVariantResolver::resolvePolicyVariants() {
  std::map<std::string, std::shared_ptr<DeclarationNode>> domains;
  for (const auto &child : m_ctx.tree->getChildren()) {
    if (child && child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getEntityType() == "_domainDefinition" ||
          decl->getEntityType() == "gameDefinition") {
        domains[decl->getName()] = decl;
      }
    }
  }

  ScopeStack scope;
  resolveForNode(m_ctx.tree, nullptr, scope, domains);
}

void PolicyVariantResolver::resolveForNode(
    const ASTNode &node, const std::shared_ptr<DeclarationNode> &activeDomain,
    ScopeStack &scope,
    const std::map<std::string, std::shared_ptr<DeclarationNode>> &domains) {
  if (!node) {
    return;
  }

  std::function<std::pair<std::string, std::vector<std::string>>(
      const std::shared_ptr<DeclarationNode> &)>
      getPolicies = [&](const std::shared_ptr<DeclarationNode> &dom)
      -> std::pair<std::string, std::vector<std::string>> {
    if (!dom) {
      return {"", {}};
    }
    std::string qPolicy;
    std::vector<std::string> vPolicy;

    auto qp = dom->getPropertyValue("qualityPolicy");
    if (qp && qp->getNodeType() != AST::None) {
      qPolicy = ASTQuery::getNodeName(qp);
    }
    auto vp = dom->getPropertyValue("variantPolicy");
    if (vp && vp->getNodeType() == AST::List) {
      for (const auto &item : vp->getChildren()) {
        if (item && item->getNodeType() == AST::String) {
          vPolicy.push_back(
              std::static_pointer_cast<ValueNode>(item)->getStringValue());
        }
      }
    }

    if (qPolicy.empty() && vPolicy.empty()) {
      auto parentProp = dom->getPropertyValue("parentDomain");
      if (parentProp && parentProp->getNodeType() != AST::None) {
        if (auto pDecl = ASTQuery::findDomainDeclaration(
                ASTQuery::getNodeName(parentProp), m_ctx.tree)) {
          return getPolicies(pDecl);
        }
      }
    }
    return {qPolicy, vPolicy};
  };

  auto rewriteNode = [&](const ASTNode &targetNode, const std::string &name) {
    if (!targetNode || targetNode->getCompilerProperty("_at")) {
      return;
    }

    auto dom = activeDomain;
    if (!dom) {
      if (auto domNode =
              CodeAnalysis::getNodeDomain(targetNode, scope, m_ctx.tree)) {
        dom = ASTQuery::findDomainDeclaration(
            ASTQuery::getDomainIdentifier(domNode, scope, m_ctx.tree),
            m_ctx.tree);
      }
    }
    if (!dom && domains.size() == 1) {
      dom = domains.begin()->second;
    }
    if (!dom) {
      return;
    }

    auto policies = getPolicies(dom);
    const std::string &qPolicy = policies.first;
    const std::vector<std::string> &vPolicy = policies.second;

    std::string tag;
    if (!qPolicy.empty()) {
      m_ctx.policyManager.resolveVariant(qPolicy, name, &tag);
    } else if (!vPolicy.empty()) {
      for (const auto &candTag : vPolicy) {
        std::string candName = name + "@" + candTag;
        if (ASTQuery::findDeclarationByName(candName, scope, m_ctx.tree,
                                            targetNode->getNamespaceList())) {
          tag = candTag;
          break;
        }
        for (const auto &pair : m_ctx.system->getImportTrees()) {
          if (ASTQuery::findDeclarationByName(
                  candName, {{nullptr, pair.second}}, nullptr,
                  targetNode->getNamespaceList())) {
            tag = candTag;
            break;
          }
        }
        if (!tag.empty()) {
          break;
        }
      }
    }

    if (!tag.empty()) {
      targetNode->setCompilerProperty(
          "_at",
          std::make_shared<ValueNode>(tag, targetNode->getFilename().c_str(),
                                      targetNode->getLine()));
    }
  };

  if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    rewriteNode(func, func->getName());
    for (const auto &prop : func->getProperties()) {
      resolveForNode(prop->getValue(), activeDomain, scope, domains);
    }
    return;
  }

  if (node->getNodeType() == AST::Entity) {
    auto ent = std::static_pointer_cast<EntityNode>(node);
    if (m_ctx.policyManager.hasAnyPolicyBinding(ent->getName())) {
      rewriteNode(ent, ent->getName());
    }
    return;
  }

  if (node->getNodeType() == AST::Stream) {
    auto stream = std::static_pointer_cast<StreamNode>(node);
    auto streamDom = activeDomain;
    if (stream->getLeft() && stream->getLeft()->getNodeType() == AST::Entity) {
      if (auto d = ASTQuery::findDomainDeclaration(
              ASTQuery::getNodeName(stream->getLeft()), m_ctx.tree)) {
        streamDom = d;
      }
    }
    if (!streamDom) {
      if (auto domNode =
              CodeAnalysis::getNodeDomain(stream, scope, m_ctx.tree)) {
        streamDom = ASTQuery::findDomainDeclaration(
            ASTQuery::getDomainIdentifier(domNode, scope, m_ctx.tree),
            m_ctx.tree);
      }
    }
    resolveForNode(stream->getLeft(), streamDom, scope, domains);
    resolveForNode(stream->getRight(), streamDom, scope, domains);
    return;
  }

  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    auto declDom = activeDomain;
    if (decl->getDomain()) {
      if (auto d = ASTQuery::findDomainDeclaration(
              ASTQuery::getDomainIdentifier(decl->getDomain(), scope,
                                            m_ctx.tree),
              m_ctx.tree)) {
        declDom = d;
      }
    }
    scope.push_back({decl, {}});
    for (const auto &prop : decl->getProperties()) {
      resolveForNode(prop->getValue(), declDom, scope, domains);
    }
    for (const auto &child : decl->getChildren()) {
      resolveForNode(child, declDom, scope, domains);
    }
    scope.pop_back();
    return;
  }

  if (node->getNodeType() == AST::Property) {
    auto prop = std::static_pointer_cast<PropertyNode>(node);
    resolveForNode(prop->getValue(), activeDomain, scope, domains);
    return;
  }

  if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      resolveForNode(expr->getValue(), activeDomain, scope, domains);
    } else {
      resolveForNode(expr->getLeft(), activeDomain, scope, domains);
      resolveForNode(expr->getRight(), activeDomain, scope, domains);
    }
    return;
  }

  for (const auto &child : node->getChildren()) {
    resolveForNode(child, activeDomain, scope, domains);
  }
}
