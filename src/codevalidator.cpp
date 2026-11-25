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

#include "stride/stridegen/codevalidator.hpp"

#include "stride/parser/memberaccessnode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/astruntime.h"
#include "stride/utils/astvalidation.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace strd;

namespace {

std::string stripDomainFramework(const std::string &domainId) {
  auto sep = domainId.find("::");
  return sep == std::string::npos ? domainId : domainId.substr(sep + 2);
}

bool literalFits(const ASTNode &node, const DataTypes::Info &to) {
  return DataTypes::literalFits(*std::static_pointer_cast<ValueNode>(node), to);
}

void addConversionWarning(const ASTNode &node, const std::string &from,
                          const std::string &to, const std::string &reason,
                          std::vector<LangError> &warnings) {
  LangError warning;
  warning.type = LangError::LossyConversion;
  warning.severity = LangError::Warning;
  if (node) {
    node->copyLocationTo(warning);
  }
  warning.errorTokens = {from, to, reason};
  warnings.push_back(warning);
}

std::string stringProperty(const ASTNode &node, const std::string &name) {
  auto property = node->getCompilerProperty(name);
  if (property && property->getNodeType() == AST::String) {
    return std::static_pointer_cast<ValueNode>(property)->getStringValue();
  }
  return {};
}

std::string trimmed(std::string text) {
  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
  text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(),
             text.end());
  return text;
}

// operandOfRiskyPromotion: node is an operand of an expression already
// reported for its promotion, which covers the operand's conversion.
void findLossyConversionsInNode(const ASTNode &node, const ASTNode &tree,
                                std::vector<LangError> &warnings,
                                std::set<std::pair<const void *, bool>> &visited,
                                bool operandOfRiskyPromotion = false) {
  if (!node || !visited.insert({node.get(), operandOfRiskyPromotion}).second) {
    return;
  }
  std::string to = stringProperty(node, "typecast");
  std::string from = stringProperty(node, "typecastFrom");
  if (!to.empty() && !from.empty() && !operandOfRiskyPromotion) {
    std::string risk = DataTypes::conversionRisk(from, to, tree);
    bool isLiteral =
        node->getNodeType() == AST::Int || node->getNodeType() == AST::Real;
    if (!risk.empty() &&
        !(isLiteral && literalFits(node, DataTypes::resolve(to, tree)))) {
      addConversionWarning(node, from, to, risk, warnings);
    }
  }
  bool riskyPromotion = false;
  if (node->getNodeType() == AST::Expression) {
    std::string risk = stringProperty(node, "promotionRisk");
    if (!risk.empty()) {
      riskyPromotion = true;
      addConversionWarning(node, trimmed(node->toText()),
                           stringProperty(node, "promotedType"), risk,
                           warnings);
    }
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    for (const auto &property :
         std::static_pointer_cast<DeclarationNode>(node)->getProperties()) {
      findLossyConversionsInNode(property->getValue(), tree, warnings, visited);
    }
  }
  for (const auto &child : node->getChildren()) {
    findLossyConversionsInNode(child, tree, warnings, visited, riskyPromotion);
  }
}

void collectBroadcastMismatches(const ASTNode &node,
                                std::vector<LangError> &errors) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Function) {
    auto widths = node->getCompilerProperty("_broadcastMismatch");
    if (widths && widths->getChildren().size() == 2) {
      auto width = [&](size_t i) {
        return std::to_string(std::static_pointer_cast<ValueNode>(
                                  widths->getChildren()[i])
                                  ->getIntValue());
      };
      LangError error;
      error.type = LangError::StreamMemberSizeMismatch;
      node->copyLocationTo(error);
      // "Stream size mismatch. 'Equal() input' provides N outs , M expected."
      error.errorTokens.push_back(width(0) + " ");
      error.errorTokens.push_back(
          std::static_pointer_cast<FunctionNode>(node)->getName() + "() input");
      error.errorTokens.push_back(width(1));
      errors.push_back(error);
    }
    for (const auto &property :
         std::static_pointer_cast<FunctionNode>(node)->getProperties()) {
      collectBroadcastMismatches(property->getValue(), errors);
    }
    return;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    for (const auto &property :
         std::static_pointer_cast<DeclarationNode>(node)->getProperties()) {
      collectBroadcastMismatches(property->getValue(), errors);
    }
    return;
  }
  if (node->getNodeType() == AST::Stream) {
    auto stream = std::static_pointer_cast<StreamNode>(node);
    collectBroadcastMismatches(stream->getLeft(), errors);
    collectBroadcastMismatches(stream->getRight(), errors);
    return;
  }
  for (const auto &child : node->getChildren()) {
    collectBroadcastMismatches(child, errors);
  }
}

std::string mainPortName(const std::shared_ptr<DeclarationNode> &decl,
                         const std::string &kind) {
  auto ports = decl ? decl->getPropertyValue("ports") : nullptr;
  if (!ports) {
    return {};
  }
  for (const auto &port : ports->getChildren()) {
    if (port &&
        (port->getNodeType() == AST::Declaration ||
         port->getNodeType() == AST::ArrayDeclaration) &&
        std::static_pointer_cast<DeclarationNode>(port)->getEntityType() ==
            kind) {
      return std::static_pointer_cast<DeclarationNode>(port)->getName();
    }
  }
  return {};
}

void collectUnsizedPorts(const ASTNode &node, ScopeStack scope,
                         const ASTNode &tree, std::vector<LangError> &errors) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    auto decl = ASTQuery::findDeclarationForNode(func, scope, tree);
    if (decl && (decl->getEntityType() == "module" ||
                 decl->getEntityType() == "loop" ||
                 decl->getEntityType() == "reaction")) {
      const std::string input = mainPortName(decl, "mainInputPort");
      const std::string output = mainPortName(decl, "mainOutputPort");
      bool inputSized = false;
      bool outputSized = false;
      for (const auto &access : ASTQuery::getUsedMemberAccesses(decl)) {
        if (access->getPropertyName() == "size") {
          inputSized |= !input.empty() && access->getEntity() == input;
          outputSized |= !output.empty() && access->getEntity() == output;
        }
      }
      auto report = [&](const std::string &port, const std::string &side,
                        const std::string &source,
                        const std::string &requirement) {
        LangError error;
        error.type = LangError::UnsizedPort;
        func->copyLocationTo(error);
        error.errorTokens = {func->getName(), side, source, port, requirement};
        errors.push_back(error);
      };
      auto outputEntity = func->getCompilerProperty("outputEntity");
      if (outputSized &&
          (!outputEntity || (outputEntity->getNodeType() != AST::Entity &&
                             outputEntity->getNodeType() != AST::Array &&
                             outputEntity->getNodeType() != AST::List))) {
        report(output, "output", "what it writes to",
               "it must write to a declared array, element or bundle");
      }
      if (inputSized && !func->getCompilerProperty("inputEntity")) {
        report(input, "input", "what feeds it", "it must be fed");
      }
    }
    for (const auto &property : func->getProperties()) {
      collectUnsizedPorts(property->getValue(), scope, tree, errors);
    }
    return;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    ScopeStack childScope = scope;
    if (decl->getEntityType() == "module" || decl->getEntityType() == "loop" ||
        decl->getEntityType() == "reaction") {
      auto entities = decl->getPropertyValue("entities");
      childScope.push_back(
          {decl, entities ? entities->getChildren() : std::vector<ASTNode>{}});
    }
    for (const auto &property : decl->getProperties()) {
      collectUnsizedPorts(property->getValue(), childScope, tree, errors);
    }
    return;
  }
  if (node->getNodeType() == AST::Stream) {
    auto stream = std::static_pointer_cast<StreamNode>(node);
    collectUnsizedPorts(stream->getLeft(), scope, tree, errors);
    collectUnsizedPorts(stream->getRight(), scope, tree, errors);
    return;
  }
  for (const auto &child : node->getChildren()) {
    collectUnsizedPorts(child, scope, tree, errors);
  }
}

bool hasConnectionBetween(const std::string &srcDom, const std::string &dstDom,
                          const ASTNode &tree) {
  if (!tree) {
    return false;
  }
  const std::string srcFw = ASTQuery::getFrameworkForDomain(srcDom, tree);
  const std::string dstFw = ASTQuery::getFrameworkForDomain(dstDom, tree);
  const std::string srcBase = stripDomainFramework(srcDom);
  const std::string dstBase = stripDomainFramework(dstDom);

  auto domainMatches = [](const ASTNode &item, const std::string &domId,
                          const std::string &domBase) {
    const std::string itemName = ASTQuery::getNodeName(item);
    return itemName == domId || itemName == domBase;
  };

  auto checkConnectionDecl =
      [&](const std::shared_ptr<DeclarationNode> &decl) -> bool {
    if (!decl || decl->getEntityType() != "connection") {
      return false;
    }
    auto srcP = decl->getPropertyValue("sourceFramework");
    auto dstP = decl->getPropertyValue("destinationFramework");
    std::string s = srcP ? ASTQuery::getNodeName(srcP) : "";
    std::string d = dstP ? ASTQuery::getNodeName(dstP) : "";
    if (!s.empty() && !d.empty() &&
        (s == srcDom || s == srcBase || (!srcFw.empty() && s == srcFw)) &&
        (d == dstDom || d == dstBase || (!dstFw.empty() && d == dstFw))) {
      return true;
    }

    auto srcDoms = decl->getPropertyValue("sourceDomains");
    auto dstDoms = decl->getPropertyValue("destinationDomains");
    bool srcMatch = false;
    if (srcDoms && srcDoms->getNodeType() == AST::List) {
      if (srcDoms->getChildren().empty()) {
        srcMatch = !s.empty() &&
                   (s == srcDom || s == srcBase || (!srcFw.empty() && s == srcFw));
      } else {
        for (const auto &item : srcDoms->getChildren()) {
          if (domainMatches(item, srcDom, srcBase)) {
            srcMatch = true;
            break;
          }
        }
      }
    }
    bool dstMatch = false;
    if (dstDoms && dstDoms->getNodeType() == AST::List) {
      if (dstDoms->getChildren().empty()) {
        dstMatch = !d.empty() &&
                   (d == dstDom || d == dstBase || (!dstFw.empty() && d == dstFw));
      } else {
        for (const auto &item : dstDoms->getChildren()) {
          if (domainMatches(item, dstDom, dstBase)) {
            dstMatch = true;
            break;
          }
        }
      }
    }
    return srcMatch && dstMatch;
  };

  for (const auto &node : tree->getChildren()) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() == "connection") {
      if (checkConnectionDecl(decl)) {
        return true;
      }
    } else if (decl->getEntityType() == "system") {
      auto conns = decl->getPropertyValue("connections");
      if (conns && conns->getNodeType() == AST::List) {
        for (const auto &item : conns->getChildren()) {
          if (!item) {
            continue;
          }
          if (item->getNodeType() == AST::Declaration) {
            if (checkConnectionDecl(
                    std::static_pointer_cast<DeclarationNode>(item))) {
              return true;
            }
          } else if (item->getNodeType() == AST::Entity) {
            if (checkConnectionDecl(
                    ASTQuery::findDeclarationForNode(item, {}, tree))) {
              return true;
            }
          }
        }
      }
    }
  }
  return false;
}

void collectSignalReads(const ASTNode &node, std::vector<ASTNode> &reads) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    reads.push_back(node);
    if (node->getNodeType() == AST::Array) {
      collectSignalReads(std::static_pointer_cast<ArrayNode>(node)->index(),
                         reads);
    }
    return;
  }
  if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      collectSignalReads(expr->getValue(), reads);
    } else {
      collectSignalReads(expr->getLeft(), reads);
      collectSignalReads(expr->getRight(), reads);
    }
    return;
  }
  if (node->getNodeType() == AST::List) {
    for (const auto &child : node->getChildren()) {
      collectSignalReads(child, reads);
    }
    return;
  }
  if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    for (const auto &prop : func->getProperties()) {
      collectSignalReads(prop->getValue(), reads);
    }
  }
}

std::string getStreamTargetDomain(const std::shared_ptr<StreamNode> &stream,
                                  const ScopeStack &scope,
                                  const ASTNode &tree) {
  ASTNode right = stream->getRight();
  ASTNode lastNode = right;
  while (right && right->getNodeType() == AST::Stream) {
    right = std::static_pointer_cast<StreamNode>(right)->getRight();
    if (right) {
      lastNode = right;
    }
  }
  if (!lastNode) {
    return {};
  }
  if (lastNode->getNodeType() == AST::List) {
    for (const auto &elem : lastNode->getChildren()) {
      std::string d = CodeAnalysis::getNodeDomainName(elem, scope, tree);
      if (!d.empty()) {
        return d;
      }
    }
  }
  return CodeAnalysis::getNodeDomainName(lastNode, scope, tree);
}

void validateStreamCrossDomainAccess(const std::shared_ptr<StreamNode> &stream,
                                     const std::string &enclosingDomain,
                                     const ScopeStack &scope,
                                     const ASTNode &tree,
                                     std::vector<LangError> &errors) {
  std::string targetDom = enclosingDomain;
  if (targetDom.empty()) {
    targetDom = getStreamTargetDomain(stream, scope, tree);
  }
  if (targetDom.empty()) {
    return;
  }

  std::vector<ASTNode> reads;
  collectSignalReads(stream->getLeft(), reads);

  ASTNode curr = stream->getRight();
  while (curr) {
    ASTNode member = curr;
    if (curr->getNodeType() == AST::Stream) {
      member = std::static_pointer_cast<StreamNode>(curr)->getLeft();
      curr = std::static_pointer_cast<StreamNode>(curr)->getRight();
    } else {
      curr = nullptr;
    }
    if (!member) {
      continue;
    }
    if (member->getNodeType() == AST::Function) {
      collectSignalReads(member, reads);
    } else if (curr != nullptr) {
      // Non-terminal stream stage (expressions, lists, arrays)
      collectSignalReads(member, reads);
    } else if (member->getNodeType() == AST::Array) {
      // Terminal array target may read another signal inside its index
      collectSignalReads(std::static_pointer_cast<ArrayNode>(member)->index(),
                         reads);
    }
  }

  for (const auto &sourceNode : reads) {
    auto decl = ASTQuery::findDeclarationForNode(sourceNode, scope, tree);
    if (!decl) {
      continue;
    }
    const std::string &entityType = decl->getEntityType();
    if (entityType != "signal" && entityType != "switch" &&
        entityType != "trigger") {
      continue;
    }
    std::string sourceDom = CodeAnalysis::getNodeDomainName(decl, scope, tree);
    if (sourceDom.empty() || sourceDom == targetDom ||
        stripDomainFramework(sourceDom) == stripDomainFramework(targetDom)) {
      continue;
    }
    if (!ASTQuery::getCommonAncestorDomain(sourceDom, targetDom, tree).empty()) {
      continue;
    }
    if (hasConnectionBetween(sourceDom, targetDom, tree)) {
      continue;
    }
    LangError error;
    error.type = LangError::InvalidCrossDomainAccess;
    sourceNode->copyLocationTo(error);
    error.errorTokens = {ASTQuery::getNodeName(sourceNode), sourceDom,
                         targetDom};
    errors.push_back(error);
  }
}

void appendDiagnostics(std::vector<LangError> &dst,
                       const std::vector<LangError> &src) {
  dst.insert(dst.end(), src.begin(), src.end());
}

} // namespace

CodeValidator::CodeValidator(const ASTNode &tree, Options options)
    : m_tree(tree), m_options(options) {
  if (m_tree) {
    validate();
  }
}

std::vector<LangError> CodeValidator::findLossyConversions(const ASTNode &tree) {
  std::vector<LangError> warnings;
  std::set<std::pair<const void *, bool>> visited;
  findLossyConversionsInNode(tree, tree, warnings, visited);

  std::vector<LangError> unique;
  std::set<std::vector<std::string>> seen;
  for (const auto &warning : warnings) {
    std::vector<std::string> key = {warning.filename,
                                    std::to_string(warning.lineNumber),
                                    std::to_string(warning.columnNumber)};
    key.insert(key.end(), warning.errorTokens.begin(),
               warning.errorTokens.end());
    if (seen.insert(key).second) {
      unique.push_back(warning);
    }
  }
  return unique;
}

std::vector<LangError>
CodeValidator::findBroadcastMismatches(const ASTNode &tree) {
  std::vector<LangError> errors;
  collectBroadcastMismatches(tree, errors);
  return errors;
}

std::vector<LangError> CodeValidator::findUnsizedPorts(const ASTNode &tree) {
  std::vector<LangError> errors;
  collectUnsizedPorts(tree, {}, tree, errors);
  return errors;
}

std::vector<LangError>
CodeValidator::findInvalidCrossDomainAccess(const ASTNode &tree) {
  std::vector<LangError> errors;
  if (!tree) {
    return errors;
  }

  std::function<void(const ASTNode &, const std::string &, ScopeStack)> visit =
      [&](const ASTNode &node, const std::string &currentDomain,
          ScopeStack scope) {
        if (!node) {
          return;
        }
        std::string dom = currentDomain;
        if (node->getNodeType() == AST::Declaration ||
            node->getNodeType() == AST::ArrayDeclaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(node);
          if (auto domProp = decl->getPropertyValue("domain")) {
            std::string resolvedDom =
                ASTQuery::getDomainIdentifier(domProp, scope, tree);
            dom = resolvedDom.empty() ? ASTQuery::getNodeName(domProp)
                                      : resolvedDom;
          }
          if (auto entities = decl->getPropertyValue("entities")) {
            scope.push_back({decl, entities->getChildren()});
          }
          if (auto streams = decl->getPropertyValue("streams")) {
            for (const auto &sNode : streams->getChildren()) {
              if (sNode && sNode->getNodeType() == AST::Stream) {
                validateStreamCrossDomainAccess(
                    std::static_pointer_cast<StreamNode>(sNode), dom, scope,
                    tree, errors);
              }
            }
          }
          if (auto entities = decl->getPropertyValue("entities")) {
            for (const auto &child : entities->getChildren()) {
              visit(child, dom, scope);
            }
          }
          return;
        }
        if (node->getNodeType() == AST::Stream) {
          validateStreamCrossDomainAccess(
              std::static_pointer_cast<StreamNode>(node), dom, scope, tree,
              errors);
          return;
        }
        for (const auto &child : node->getChildren()) {
          visit(child, dom, scope);
        }
      };

  visit(tree, "", {});
  return errors;
}

std::vector<LangError> CodeValidator::findResourceErrors(ASTNode tree) {
  std::vector<LangError> errors;
  if (!tree) {
    return errors;
  }

  auto stripQualifier = [](std::string name) {
    size_t pos = name.rfind("::");
    if (pos != std::string::npos) {
      return name.substr(pos + 2);
    }
    return name;
  };

  auto getResourceList = [&](const std::shared_ptr<DeclarationNode> &decl,
                             const std::string &propName) {
    std::vector<std::string> list;
    if (!decl) {
      return list;
    }
    auto prop = decl->getPropertyValue(propName);
    if (!prop || prop->getNodeType() == AST::None) {
      return list;
    }
    auto addItem = [&](const ASTNode &item) {
      if (!item || item->getNodeType() == AST::None) {
        return;
      }
      std::string name;
      if (item->getNodeType() == AST::String) {
        name = std::static_pointer_cast<ValueNode>(item)->getStringValue();
      } else {
        name = ASTQuery::getNodeName(item);
      }
      name = stripQualifier(name);
      if (!name.empty() && name != "none" &&
          std::find(list.begin(), list.end(), name) == list.end()) {
        list.push_back(name);
      }
    };
    if (prop->getNodeType() == AST::List) {
      for (const auto &child : prop->getChildren()) {
        addItem(child);
      }
    } else {
      addItem(prop);
    }
    return list;
  };

  std::map<std::string, std::shared_ptr<DeclarationNode>> allDomains;
  std::vector<std::string> domainOrder;
  std::map<std::string, std::shared_ptr<DeclarationNode>> allResources;
  std::vector<std::shared_ptr<DeclarationNode>> allDevices;
  std::vector<std::shared_ptr<DeclarationNode>> allGpioPins;
  std::map<std::string, std::string> pinFunctionPeripherals;

  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() != AST::Declaration &&
        node->getNodeType() != AST::ArrayDeclaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    const std::string &et = decl->getEntityType();
    if (et == "_domainDefinition" || et == "gameDefinition") {
      if (!allDomains.count(decl->getName())) {
        domainOrder.push_back(decl->getName());
      }
      allDomains[decl->getName()] = decl;
    } else if (et == "resource" || et == "domainResource") {
      allResources[decl->getName()] = decl;
    } else if (et == "device") {
      allDevices.push_back(decl);
    } else if (et == "gpioPin") {
      allGpioPins.push_back(decl);
    } else {
      auto periphProp = decl->getPropertyValue("peripheral");
      if (periphProp && periphProp->getNodeType() == AST::String) {
        pinFunctionPeripherals[decl->getName()] =
            std::static_pointer_cast<ValueNode>(periphProp)->getStringValue();
      }
    }
  }

  if (allDomains.empty()) {
    return errors;
  }

  // Helper to resolve the executing domain of a top-level stream
  auto resolveStreamDomain =
      [&](const std::shared_ptr<StreamNode> &stream) -> std::string {
    std::string d = stripQualifier(getStreamTargetDomain(stream, {}, tree));
    if (!d.empty() && allDomains.count(d)) {
      return d;
    }
    std::string found;
    std::function<void(const ASTNode &)> scan = [&](const ASTNode &n) {
      if (!n || !found.empty()) {
        return;
      }
      if (n->getNodeType() == AST::Stream) {
        auto s = std::static_pointer_cast<StreamNode>(n);
        scan(s->getRight());
        scan(s->getLeft());
        return;
      }
      if (n->getNodeType() == AST::Function) {
        auto fn = std::static_pointer_cast<FunctionNode>(n);
        if (fn->getName() == "Sleep") {
          if (auto domProp = fn->getPropertyValue("domain")) {
            std::string cand = stripQualifier(ASTQuery::getNodeName(domProp));
            if (allDomains.count(cand)) {
              found = cand;
              return;
            }
          }
        }
      }
      std::string cand =
          stripQualifier(CodeAnalysis::getNodeDomainName(n, {}, tree));
      if (!cand.empty() && allDomains.count(cand)) {
        found = cand;
      }
    };
    scan(stream);
    if (!found.empty()) {
      return found;
    }
    if (allDomains.count("RootDomain")) {
      return "RootDomain";
    }
    if (allDomains.size() == 1) {
      return allDomains.begin()->first;
    }
    return {};
  };

  // Determine active domains
  std::set<std::string> activeDomainSet;
  for (const auto &domName : domainOrder) {
    auto domDecl = allDomains[domName];
    if (!domDecl->getCompilerProperty("framework")) {
      activeDomainSet.insert(domName);
    }
    for (const char *propName :
         {"onLoad", "onInit", "onReset", "stateMachines", "preSubdomains",
          "postSubdomains"}) {
      auto p = domDecl->getPropertyValue(propName);
      if (p && p->getNodeType() == AST::List && !p->getChildren().empty()) {
        activeDomainSet.insert(domName);
      }
    }
  }
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      std::string d =
          resolveStreamDomain(std::static_pointer_cast<StreamNode>(node));
      if (!d.empty()) {
        activeDomainSet.insert(d);
      }
    } else if (node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() != "_domainDefinition" &&
          decl->getEntityType() != "gameDefinition" &&
          !decl->getCompilerProperty("framework")) {
        std::string d =
            stripQualifier(CodeAnalysis::getNodeDomainName(decl, {}, tree));
        if (!d.empty() && allDomains.count(d)) {
          activeDomainSet.insert(d);
        }
      }
    }
  }
  if (activeDomainSet.empty() && allDomains.count("RootDomain")) {
    activeDomainSet.insert("RootDomain");
  }
  // Propagate activity to parentDomain, preSubdomains, and postSubdomains
  {
    std::vector<std::string> work(activeDomainSet.begin(),
                                  activeDomainSet.end());
    while (!work.empty()) {
      std::string curr = work.back();
      work.pop_back();
      auto it = allDomains.find(curr);
      if (it == allDomains.end() || !it->second) {
        continue;
      }
      auto domDecl = it->second;
      if (auto parentProp = domDecl->getPropertyValue("parentDomain")) {
        if (parentProp->getNodeType() != AST::None) {
          std::string pName = stripQualifier(ASTQuery::getNodeName(parentProp));
          if (allDomains.count(pName) && activeDomainSet.insert(pName).second) {
            work.push_back(pName);
          }
        }
      }
      for (const char *subProp : {"preSubdomains", "postSubdomains"}) {
        for (const auto &subName : getResourceList(domDecl, subProp)) {
          if (allDomains.count(subName) &&
              activeDomainSet.insert(subName).second) {
            work.push_back(subName);
          }
        }
      }
    }
  }

  std::vector<std::string> activeDomains;
  for (const auto &domName : domainOrder) {
    if (activeDomainSet.count(domName)) {
      activeDomains.push_back(domName);
    }
  }

  std::set<std::vector<std::string>> seenErrors;
  auto emitError = [&](LangError::ErrorType type, const ASTNode &at,
                       std::vector<std::string> tokens) {
    std::vector<std::string> key = {
        std::to_string(static_cast<int>(type)),
        at ? at->getFilename() : "",
        std::to_string(at ? at->getLine() : 0)};
    key.insert(key.end(), tokens.begin(), tokens.end());
    if (!seenErrors.insert(key).second) {
      return;
    }
    LangError err;
    err.type = type;
    if (at) {
      at->copyLocationTo(err);
    }
    err.errorTokens = std::move(tokens);
    errors.push_back(std::move(err));
  };

  auto isIsolatedDomain = [&](const std::shared_ptr<DeclarationNode> &domDecl) {
    if (!domDecl) {
      return false;
    }
    auto execProp = domDecl->getPropertyValue("execution");
    if (!execProp || execProp->getNodeType() == AST::None) {
      return false;
    }
    if (auto origin = execProp->getCompilerProperty("resolvedFrom")) {
      if (origin->getNodeType() == AST::Declaration) {
        std::string name =
            std::static_pointer_cast<DeclarationNode>(origin)->getName();
        if (name == "ExecutionInterrupt" || name == "ExecutionHardware" ||
            name == "ExecutionThread") {
          return true;
        }
      }
    }
    if (execProp->getNodeType() == AST::Int) {
      int64_t val = std::static_pointer_cast<ValueNode>(execProp)->getIntValue();
      return val == 1 || val == 2 || val == 4;
    }
    std::string mode = stripQualifier(ASTQuery::getNodeName(execProp));
    return mode == "ExecutionInterrupt" || mode == "ExecutionHardware" ||
           mode == "ExecutionThread";
  };

  // Compute EffectiveResources(D) and track resource claims
  std::map<std::string, std::set<std::string>> effectiveResources;
  std::map<std::string, std::vector<std::pair<std::string, ASTNode>>>
      resourceClaims;

  for (const auto &domName : activeDomains) {
    auto domDecl = allDomains[domName];
    std::set<std::string> providedDomRes;
    {
      std::set<std::string> visitedAnc;
      auto curr = domDecl;
      while (curr && visitedAnc.insert(curr->getName()).second) {
        for (const auto &pr : getResourceList(curr, "providesResources")) {
          providedDomRes.insert(pr);
        }
        auto pProp = curr->getPropertyValue("parentDomain");
        if (!pProp || pProp->getNodeType() == AST::None) {
          break;
        }
        std::string pName = stripQualifier(ASTQuery::getNodeName(pProp));
        curr = allDomains.count(pName) ? allDomains[pName] : nullptr;
      }
    }

    auto directReq = getResourceList(domDecl, "requiresResources");
    auto directProv = getResourceList(domDecl, "providesResources");

    for (const auto &r : directReq) {
      resourceClaims[r].push_back({domName, domDecl});
    }

    std::set<std::string> &eff = effectiveResources[domName];
    std::vector<std::string> queue;
    auto addEff = [&](const std::string &r) {
      if (!r.empty() && eff.insert(r).second) {
        queue.push_back(r);
      }
    };

    for (const auto &r : directReq) {
      addEff(r);
    }
    for (const auto &r : directProv) {
      addEff(r);
    }
    for (const auto &r : providedDomRes) {
      addEff(r);
    }

    // Cooperative/inline subdomains without explicit requiresResources inherit
    // from their cooperative/inline parentDomain chain.
    if (directReq.empty() && !isIsolatedDomain(domDecl)) {
      std::set<std::string> visitedAnc;
      auto curr = domDecl;
      while (curr && visitedAnc.insert(curr->getName()).second) {
        auto pProp = curr->getPropertyValue("parentDomain");
        if (!pProp || pProp->getNodeType() == AST::None) {
          break;
        }
        std::string pName = stripQualifier(ASTQuery::getNodeName(pProp));
        auto parentDecl = allDomains.count(pName) ? allDomains[pName] : nullptr;
        if (!parentDecl || isIsolatedDomain(parentDecl)) {
          break;
        }
        auto pReq = getResourceList(parentDecl, "requiresResources");
        if (!pReq.empty()) {
          for (const auto &r : pReq) {
            addEff(r);
          }
          break;
        }
        curr = parentDecl;
      }
    }

    while (!queue.empty()) {
      std::string r = queue.back();
      queue.pop_back();
      auto resIt = allResources.find(r);
      if (resIt == allResources.end() || !resIt->second) {
        continue;
      }
      auto resDecl = resIt->second;
      for (const auto &dep : getResourceList(resDecl, "dependsOn")) {
        auto depIt = allResources.find(dep);
        if (depIt != allResources.end() && depIt->second &&
            depIt->second->getEntityType() == "domainResource") {
          if (!providedDomRes.count(dep) &&
              std::find(directReq.begin(), directReq.end(), dep) ==
                  directReq.end()) {
            emitError(LangError::MissingDomainResource, domDecl,
                      {r, dep, domName});
          }
        }
        addEff(dep);
      }
    }
  }

  // Enforce exclusivity, maxInstances, and peripheral conflicts
  std::set<std::string> runtimeReserves;
  for (const auto &dev : allDevices) {
    for (const auto &resv : getResourceList(dev, "runtimeReserves")) {
      runtimeReserves.insert(resv);
    }
  }

  for (const auto &[resName, claims] : resourceClaims) {
    auto resIt = allResources.find(resName);
    if (resIt == allResources.end() || !resIt->second) {
      continue;
    }
    auto resDecl = resIt->second;

    bool isExclusive = false;
    if (auto exProp = resDecl->getPropertyValue("exclusive")) {
      if (exProp->getNodeType() == AST::Switch) {
        isExclusive =
            std::static_pointer_cast<ValueNode>(exProp)->getSwitchValue();
      }
    }

    if (isExclusive && claims.size() > 1) {
      for (size_t i = 1; i < claims.size(); ++i) {
        emitError(LangError::ExclusiveResourceConflict, claims[i].second,
                  {resName, claims[0].first, claims[i].first});
      }
    }

    if (auto maxProp = resDecl->getPropertyValue("maxInstances")) {
      if (maxProp->getNodeType() == AST::Int) {
        int64_t maxInst =
            std::static_pointer_cast<ValueNode>(maxProp)->getIntValue();
        if (maxInst > 0 && static_cast<int64_t>(claims.size()) > maxInst) {
          emitError(LangError::ResourceInstanceLimitExceeded,
                    claims[static_cast<size_t>(maxInst)].second,
                    {resName, std::to_string(maxInst),
                     std::to_string(claims.size())});
        }
      }
    }

    if (auto periphProp = resDecl->getPropertyValue("peripheral")) {
      if (periphProp->getNodeType() == AST::String) {
        std::string periph =
            std::static_pointer_cast<ValueNode>(periphProp)->getStringValue();
        if (!periph.empty() && periph != "none") {
          for (const auto &claim : claims) {
            auto domDecl =
                allDomains.count(claim.first) ? allDomains[claim.first] : nullptr;
            bool userOrInterrupt =
                !domDecl || !domDecl->getCompilerProperty("framework") ||
                isIsolatedDomain(domDecl);
            if (isExclusive && userOrInterrupt &&
                runtimeReserves.count(periph)) {
              emitError(LangError::InvalidHardware, claim.second,
                        {"resource '" + resName + "' uses peripheral '" +
                         periph + "', which is reserved by the runtime"});
            }
            if (isExclusive) {
              for (const auto &pinDecl : allGpioPins) {
                auto modeProp = pinDecl->getPropertyValue("mode");
                std::string modeName =
                    modeProp ? stripQualifier(ASTQuery::getNodeName(modeProp))
                             : "";
                if (modeName != "PinFunction") {
                  continue;
                }
                auto fnProp = pinDecl->getPropertyValue("function");
                std::string fnName =
                    fnProp ? stripQualifier(ASTQuery::getNodeName(fnProp)) : "";
                if (fnName.empty()) {
                  continue;
                }
                std::string pinPeriph;
                auto pIt = pinFunctionPeripherals.find(fnName);
                if (pIt != pinFunctionPeripherals.end()) {
                  pinPeriph = pIt->second;
                } else if (fnProp) {
                  auto fnDecl = ASTQuery::findDeclarationForNode(fnProp, {}, tree);
                  if (fnDecl) {
                    auto pp = fnDecl->getPropertyValue("peripheral");
                    if (pp && pp->getNodeType() == AST::String) {
                      pinPeriph =
                          std::static_pointer_cast<ValueNode>(pp)->getStringValue();
                    }
                  }
                }
                if (pinPeriph == periph) {
                  emitError(LangError::ExclusiveResourceConflict, pinDecl,
                            {periph, claim.first, pinDecl->getName()});
                }
              }
            }
          }
        }
      }
    }
  }

  // Operation resource checking per active domain
  auto checkDeclResources =
      [&](const std::shared_ptr<DeclarationNode> &opDecl,
          const std::string &displayOp, const ASTNode &reportNode,
          const std::string &domName) {
        if (!opDecl || !allDomains.count(domName)) {
          return;
        }
        const auto &eff = effectiveResources[domName];
        std::vector<std::string> reqQueue = getResourceList(opDecl, "resources");
        std::set<std::string> visitedReq(reqQueue.begin(), reqQueue.end());
        for (size_t idx = 0; idx < reqQueue.size(); ++idx) {
          std::string req = reqQueue[idx];
          if (!eff.count(req)) {
            emitError(LangError::MissingDomainResource,
                      reportNode ? reportNode : opDecl,
                      {displayOp, req, domName});
          }
          auto rIt = allResources.find(req);
          if (rIt != allResources.end() && rIt->second) {
            for (const auto &dep : getResourceList(rIt->second, "dependsOn")) {
              if (visitedReq.insert(dep).second) {
                reqQueue.push_back(dep);
              }
            }
          }
        }
      };

  std::function<void(const ASTNode &, const std::string &, const ScopeStack &,
                     const ASTNode &, const std::string &,
                     std::set<const void *> &)>
      walkNode = [&](const ASTNode &node, const std::string &domName,
                     const ScopeStack &scope, const ASTNode &topReportNode,
                     const std::string &topOpName,
                     std::set<const void *> &visitedCallables) {
        if (!node || domName.empty()) {
          return;
        }
        switch (node->getNodeType()) {
        case AST::Stream: {
          auto s = std::static_pointer_cast<StreamNode>(node);
          walkNode(s->getLeft(), domName, scope, topReportNode, topOpName,
                   visitedCallables);
          walkNode(s->getRight(), domName, scope, topReportNode, topOpName,
                   visitedCallables);
          break;
        }
        case AST::List: {
          for (const auto &child : node->getChildren()) {
            walkNode(child, domName, scope, topReportNode, topOpName,
                     visitedCallables);
          }
          break;
        }
        case AST::Expression: {
          auto expr = std::static_pointer_cast<ExpressionNode>(node);
          if (expr->isUnary()) {
            walkNode(expr->getValue(), domName, scope, topReportNode, topOpName,
                     visitedCallables);
          } else {
            walkNode(expr->getLeft(), domName, scope, topReportNode, topOpName,
                     visitedCallables);
            walkNode(expr->getRight(), domName, scope, topReportNode, topOpName,
                     visitedCallables);
          }
          break;
        }
        case AST::Entity:
        case AST::Array: {
          ASTNode rep = topReportNode ? topReportNode : node;
          auto decl = ASTQuery::findDeclarationForNode(node, scope, tree);
          if (decl) {
            std::string rawName = decl->getName();
            std::string displayOp =
                (!topOpName.empty() && !rawName.empty() && rawName[0] == '_')
                    ? topOpName
                    : rawName;
            checkDeclResources(decl, displayOp, rep, domName);
            auto typeDecl = ASTQuery::findTypeDeclaration(decl, scope, tree);
            if (typeDecl) {
              checkDeclResources(typeDecl, displayOp, rep, domName);
            }
          }
          if (node->getNodeType() == AST::Array) {
            walkNode(std::static_pointer_cast<ArrayNode>(node)->index(), domName,
                     scope, topReportNode, topOpName, visitedCallables);
          }
          break;
        }
        case AST::Function: {
          auto func = std::static_pointer_cast<FunctionNode>(node);
          ASTNode rep = topReportNode ? topReportNode : node;
          std::string fnName = stripQualifier(func->getName());
          std::string displayOp =
              (!topOpName.empty() && !fnName.empty() && fnName[0] == '_')
                  ? topOpName
                  : fnName;
          std::string nextTopOp = topOpName;
          if (nextTopOp.empty() && !fnName.empty() && fnName[0] != '_') {
            nextTopOp = fnName;
          }

          for (const auto &prop : func->getProperties()) {
            if (prop) {
              walkNode(prop->getValue(), domName, scope, topReportNode,
                       topOpName, visitedCallables);
            }
          }

          if (fnName == "Sleep") {
            std::string targetDom = domName;
            if (auto domProp = func->getPropertyValue("domain")) {
              std::string cand = stripQualifier(ASTQuery::getNodeName(domProp));
              if (allDomains.count(cand)) {
                targetDom = cand;
              }
            }
            auto targetDomDecl =
                allDomains.count(targetDom) ? allDomains[targetDom] : nullptr;
            std::shared_ptr<DeclarationNode> sleepModDecl;
            if (targetDomDecl) {
              if (auto smProp = targetDomDecl->getPropertyValue("sleepModule")) {
                if (smProp->getNodeType() != AST::None) {
                  sleepModDecl =
                      ASTQuery::findDeclarationForNode(smProp, scope, tree);
                }
              }
            }
            if (!sleepModDecl) {
              sleepModDecl =
                  ASTQuery::findDeclarationByName("__AvrSleepMs", {}, tree);
            }
            if (sleepModDecl) {
              checkDeclResources(sleepModDecl, "Sleep", rep, targetDom);
              if (targetDom != domName) {
                checkDeclResources(sleepModDecl, "Sleep", rep, domName);
              }
            } else if (targetDomDecl && isIsolatedDomain(targetDomDecl)) {
              emitError(LangError::MissingDomainResource, rep,
                        {"Sleep", "_SleepTimer", targetDom});
            }
          }

          auto decl = ASTQuery::findDeclarationForNode(func, scope, tree);
          if (decl) {
            checkDeclResources(decl, displayOp, rep, domName);
            if (visitedCallables.insert(decl.get()).second) {
              ScopeStack subScope = scope;
              if (auto entities = decl->getPropertyValue("entities")) {
                if (entities->getNodeType() == AST::List) {
                  subScope.push_back({decl, entities->getChildren()});
                  for (const auto &ent : entities->getChildren()) {
                    if (ent->getNodeType() == AST::Declaration ||
                        ent->getNodeType() == AST::ArrayDeclaration) {
                      auto entDecl =
                          std::static_pointer_cast<DeclarationNode>(ent);
                      checkDeclResources(entDecl, displayOp, rep, domName);
                      auto tDecl =
                          ASTQuery::findTypeDeclaration(entDecl, subScope, tree);
                      if (tDecl) {
                        checkDeclResources(tDecl, displayOp, rep, domName);
                      }
                      for (const auto &p : entDecl->getProperties()) {
                        if (p) {
                          walkNode(p->getValue(), domName, subScope, rep,
                                   nextTopOp, visitedCallables);
                        }
                      }
                    }
                  }
                }
              }
              for (const auto &desc :
                   ASTQuery::getContainedStreamEntities(decl, subScope, tree)) {
                for (const auto &s : desc.streams) {
                  walkNode(s, domName, subScope, rep, nextTopOp,
                           visitedCallables);
                }
              }
              visitedCallables.erase(decl.get());
            }
          }
          break;
        }
        default:
          break;
        }
      };

  // Walk top-level streams
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      std::string domName = resolveStreamDomain(stream);
      if (!domName.empty() && activeDomainSet.count(domName)) {
        std::set<const void *> visitedCallables;
        walkNode(stream, domName, {}, nullptr, "", visitedCallables);
      }
    } else if (node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() != "_domainDefinition" &&
          decl->getEntityType() != "gameDefinition" &&
          !decl->getCompilerProperty("framework")) {
        std::string domName =
            stripQualifier(CodeAnalysis::getNodeDomainName(decl, {}, tree));
        if (!domName.empty() && activeDomainSet.count(domName)) {
          checkDeclResources(decl, decl->getName(), decl, domName);
          auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, tree);
          if (typeDecl) {
            checkDeclResources(typeDecl, decl->getName(), decl, domName);
          }
        }
      }
    }
  }

  // Walk onLoad, onInit, onReset, and stateMachines of each active domain
  for (const auto &domName : activeDomains) {
    auto domDecl = allDomains[domName];
    for (const char *hook : {"onLoad", "onInit", "onReset"}) {
      if (auto prop = domDecl->getPropertyValue(hook)) {
        std::set<const void *> visitedCallables;
        walkNode(prop, domName, {}, nullptr, "", visitedCallables);
      }
    }
    if (auto smProp = domDecl->getPropertyValue("stateMachines")) {
      if (smProp->getNodeType() == AST::List) {
        for (const auto &smNode : smProp->getChildren()) {
          auto smDecl = ASTQuery::findDeclarationForNode(smNode, {}, tree);
          if (smDecl) {
            std::set<const void *> visitedCallables;
            visitedCallables.insert(smDecl.get());
            checkDeclResources(smDecl, smDecl->getName(), smNode, domName);
            for (const auto &desc :
                 ASTQuery::getContainedStreamEntities(smDecl, {}, tree)) {
              for (const auto &s : desc.streams) {
                walkNode(s, domName, {}, nullptr, smDecl->getName(),
                         visitedCallables);
              }
            }
          }
        }
      }
    }

    // Check 32-instruction hardware limit for ExecutionHardware PIO domains
    auto execProp = domDecl->getPropertyValue("execution");
    bool isHardwareExec = false;
    if (execProp && execProp->getNodeType() != AST::None) {
      if (auto origin = execProp->getCompilerProperty("resolvedFrom")) {
        if (origin->getNodeType() == AST::Declaration &&
            std::static_pointer_cast<DeclarationNode>(origin)->getName() ==
                "ExecutionHardware") {
          isHardwareExec = true;
        }
      } else if (execProp->getNodeType() == AST::Int) {
        isHardwareExec =
            std::static_pointer_cast<ValueNode>(execProp)->getIntValue() == 4;
      } else {
        isHardwareExec =
            stripQualifier(ASTQuery::getNodeName(execProp)) == "ExecutionHardware";
      }
    }
    if (isHardwareExec && effectiveResources[domName].count("_Pio0")) {
      static const std::set<std::string> pioInstrModules = {
          "PioPull",     "PioPush",    "PioShiftOut", "PioShiftIn",
          "PioSetPins",  "PioDelay",   "PioWaitPin",  "PioIrq",
          "DigitalOut",  "Led",        "_GpioWrite"};
      size_t pioInstrCount = 0;
      auto countPioStream = [&](const std::shared_ptr<StreamNode> &st) {
        if (!st) {
          return;
        }
        std::vector<ASTNode> chain;
        ASTNode curr = st;
        while (curr && curr->getNodeType() == AST::Stream) {
          auto sn = std::static_pointer_cast<StreamNode>(curr);
          chain.push_back(sn->getLeft());
          curr = sn->getRight();
        }
        if (curr) {
          chain.push_back(curr);
        }
        size_t before = pioInstrCount;
        for (const auto &m : chain) {
          if (m && m->getNodeType() == AST::Function) {
            std::string fn = stripQualifier(
                std::static_pointer_cast<FunctionNode>(m)->getName());
            if (pioInstrModules.count(fn)) {
              ++pioInstrCount;
            }
          }
        }
        if (pioInstrCount == before && chain.size() == 2 && chain[1] &&
            chain[1]->getNodeType() == AST::Entity) {
          std::string dst = stripQualifier(ASTQuery::getNodeName(chain[1]));
          auto dstDecl = ASTQuery::findDeclarationByName(dst, {}, tree);
          if (dstDecl && dstDecl->getEntityType() == "state") {
            ++pioInstrCount; // JMP instruction
          } else if (chain[0] && chain[0]->getNodeType() == AST::Int) {
            ++pioInstrCount; // SET scratch register
          }
        }
      };

      for (const auto &node : tree->getChildren()) {
        if (node->getNodeType() == AST::Stream) {
          auto st = std::static_pointer_cast<StreamNode>(node);
          if (resolveStreamDomain(st) == domName) {
            countPioStream(st);
          }
        }
      }
      if (auto smProp = domDecl->getPropertyValue("stateMachines")) {
        if (smProp->getNodeType() == AST::List) {
          std::set<std::string> visitedStates;
          std::function<void(const std::string &)> countState =
              [&](const std::string &sName) {
                if (sName.empty() || !visitedStates.insert(sName).second) {
                  return;
                }
                auto sDecl = ASTQuery::findDeclarationByName(sName, {}, tree);
                if (!sDecl || sDecl->getEntityType() != "state") {
                  return;
                }
                for (const auto &desc :
                     ASTQuery::getContainedStreamEntities(sDecl, {}, tree)) {
                  for (const auto &st : desc.streams) {
                    countPioStream(st);
                    std::function<void(const ASTNode &)> findNext =
                        [&](const ASTNode &n) {
                          if (!n) {
                            return;
                          }
                          if (n->getNodeType() == AST::Stream) {
                            auto sn = std::static_pointer_cast<StreamNode>(n);
                            findNext(sn->getLeft());
                            findNext(sn->getRight());
                          } else if (n->getNodeType() == AST::Entity) {
                            countState(stripQualifier(ASTQuery::getNodeName(n)));
                          }
                        };
                    findNext(st);
                  }
                }
              };
          for (const auto &smNode : smProp->getChildren()) {
            countState(stripQualifier(ASTQuery::getNodeName(smNode)));
          }
        }
      }
      if (pioInstrCount > 32) {
        emitError(LangError::PioInstructionOverflow, domDecl,
                  {domName, std::to_string(pioInstrCount), "_Pio0"});
      }
    }
  }

  return errors;
}

void CodeValidator::validate() {
  m_errors.clear();
  m_warnings.clear();
  if (!m_tree) {
    return;
  }

  ASTValidation::validatePlatform(m_tree, m_errors);
  ASTValidation::validateTypes(m_tree, m_errors, {}, m_tree, {}, "");
  ASTValidation::validateShadowing(m_tree, m_errors);
  ASTValidation::validateDataTypes(m_tree, m_errors);
  m_warnings = findLossyConversions(m_tree);

  ASTValidation::validateArrayIndices(m_tree, m_errors);
  validateArraySizes(m_tree, m_errors, {});
  ASTValidation::validateSymbolUniqueness({{nullptr, m_tree->getChildren()}},
                                          m_tree, m_errors);
  validateStreamSizes(m_tree, m_errors, {});
  appendDiagnostics(m_errors, findBroadcastMismatches(m_tree));
  appendDiagnostics(m_errors, ASTValidation::findConstantRewrites(m_tree));
  appendDiagnostics(m_errors, findUnsizedPorts(m_tree));
  appendDiagnostics(m_errors, ASTValidation::findReactionMainInputs(m_tree));
  appendDiagnostics(m_errors, findInvalidCrossDomainAccess(m_tree));
  validateLoopIteratorTripCounts(m_tree, m_errors);
  appendDiagnostics(m_errors,
                    ASTValidation::findInvalidConnectionProperties(m_tree));
  appendDiagnostics(m_errors, findResourceErrors(m_tree));
  validateRates(m_tree);
  validateConstraints(m_tree);

  sortDiagnostics();
}

void CodeValidator::validateLoopIteratorTripCounts(
    const ASTNode &tree, std::vector<LangError> &errors) {
  auto validateLoopDeclIterators =
      [&](const std::shared_ptr<DeclarationNode> &decl,
          const std::shared_ptr<FunctionNode> &callFunc) {
        auto entities = decl->getPropertyValue("entities");
        if (!entities || entities->getNodeType() != AST::List) {
          return;
        }
        ScopeStack loopScope = {{decl, entities->getChildren()}};
        const std::string inPort = mainPortName(decl, "mainInputPort");
        const std::string outPort = mainPortName(decl, "mainOutputPort");
        std::string inPortEntity;
        std::string outPortEntity;
        if (auto ports = decl->getPropertyValue("ports")) {
          for (const auto &p : ports->getChildren()) {
            if (!p || p->getNodeType() != AST::Declaration) {
              continue;
            }
            auto pd = std::static_pointer_cast<DeclarationNode>(p);
            if (pd->getEntityType() == "mainInputPort") {
              if (auto pb = pd->getPropertyValue("entity")) {
                inPortEntity = ASTQuery::getNodeName(pb);
              }
            } else if (pd->getEntityType() == "mainOutputPort") {
              if (auto pb = pd->getPropertyValue("entity")) {
                outPortEntity = ASTQuery::getNodeName(pb);
              }
            }
          }
        }

        struct IteratorTripInfo {
          std::shared_ptr<DeclarationNode> decl;
          int64_t trips;
          bool usedCallSitePort;
        };
        std::vector<IteratorTripInfo> iterators;
        bool anyCallSitePort = false;

        for (const auto &child : entities->getChildren()) {
          if (!child || child->getNodeType() != AST::Declaration) {
            continue;
          }
          auto itDecl = std::static_pointer_cast<DeclarationNode>(child);
          if (itDecl->getEntityType() != "iterator") {
            continue;
          }
          auto entityProp = itDecl->getPropertyValue("entity");
          if (!entityProp || entityProp->getNodeType() == AST::None) {
            continue;
          }
          auto targetDecl =
              ASTQuery::findDeclarationForNode(entityProp, loopScope, tree);
          if (!targetDecl) {
            continue;
          }

          int64_t entitySize = -1;
          bool thisUsedCallSitePort = false;
          if (targetDecl->getNodeType() == AST::ArrayDeclaration) {
            std::vector<LangError> szErrors;
            int sz = ASTQuery::getEntityDeclaredSize(targetDecl, loopScope,
                                                     tree, &szErrors);
            if (szErrors.empty() && sz > 0) {
              entitySize = sz;
            } else if (callFunc) {
              auto arrIdx = targetDecl->getArrayIndex();
              if (arrIdx && arrIdx->index() &&
                  !arrIdx->index()->getChildren().empty()) {
                auto idxNode = arrIdx->index()->getChildren()[0];
                if (idxNode && idxNode->getNodeType() == AST::MemberAccess) {
                  auto ma = std::static_pointer_cast<MemberAccessNode>(idxNode);
                  if (ma->getPropertyName() == "size") {
                    if ((!inPort.empty() && ma->getEntity() == inPort) ||
                        (!inPortEntity.empty() &&
                         ma->getEntity() == inPortEntity)) {
                      if (auto inBlk =
                              callFunc->getCompilerProperty("inputEntity")) {
                        int inSz = CodeAnalysis::getNodeNumOutputs(inBlk, {},
                                                                   tree);
                        if (inSz > 0) {
                          entitySize = inSz;
                          thisUsedCallSitePort = true;
                        }
                      }
                    } else if ((!outPort.empty() &&
                                ma->getEntity() == outPort) ||
                               (!outPortEntity.empty() &&
                                ma->getEntity() == outPortEntity)) {
                      if (auto outBlk =
                              callFunc->getCompilerProperty("outputEntity")) {
                        int outSz = CodeAnalysis::getNodeNumInputs(outBlk, {},
                                                                   tree);
                        if (outSz > 0) {
                          entitySize = outSz;
                          thisUsedCallSitePort = true;
                        }
                      }
                    }
                  }
                }
              }
            }
          } else if (targetDecl->getEntityType() == "buffer") {
            int sz =
                CodeAnalysis::getTypeNumOutputs(targetDecl, loopScope, tree);
            if (sz > 0) {
              entitySize = sz;
            }
          }
          if (entitySize <= 0) {
            continue;
          }

          int64_t offsetVal = 0;
          if (auto offProp = itDecl->getPropertyValue("offset")) {
            if (offProp->getNodeType() != AST::None) {
              std::vector<LangError> evalErrs;
              int64_t v = ASTFunctions::evaluateConstInteger(
                  offProp, loopScope, tree, &evalErrs);
              if (!evalErrs.empty()) {
                continue;
              }
              offsetVal = v;
            }
          }
          int64_t strideVal = 1;
          if (auto strProp = itDecl->getPropertyValue("stride")) {
            if (strProp->getNodeType() != AST::None) {
              std::vector<LangError> evalErrs;
              int64_t v = ASTFunctions::evaluateConstInteger(
                  strProp, loopScope, tree, &evalErrs);
              if (!evalErrs.empty() || v <= 0) {
                continue;
              }
              strideVal = v;
            }
          }
          int64_t trips =
              (entitySize > offsetVal)
                  ? (entitySize - offsetVal + strideVal - 1) / strideVal
                  : 0;
          anyCallSitePort |= thisUsedCallSitePort;
          iterators.push_back({itDecl, trips, thisUsedCallSitePort});
        }

        if (iterators.size() <= 1) {
          return;
        }
        if (callFunc && !anyCallSitePort) {
          return;
        }
        const int64_t expectedTrips = iterators.front().trips;
        for (size_t i = 1; i < iterators.size(); ++i) {
          if (iterators[i].trips != expectedTrips) {
            LangError error;
            error.type = LangError::ArraySizeMismatch;
            if (callFunc) {
              callFunc->copyLocationTo(error);
            } else {
              iterators[i].decl->copyLocationTo(error);
            }
            error.errorTokens.push_back(iterators[i].decl->getName());
            error.errorTokens.push_back(std::to_string(expectedTrips));
            error.errorTokens.push_back(std::to_string(iterators[i].trips));
            errors.push_back(error);
          }
        }
      };

  std::function<void(const ASTNode &)> checkLoopIterators =
      [&](const ASTNode &node) {
        if (!node) {
          return;
        }
        if (node->getNodeType() == AST::Declaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(node);
          if (decl->getEntityType() == "loop") {
            validateLoopDeclIterators(decl, nullptr);
          }
          if (auto entities = decl->getPropertyValue("entities")) {
            for (const auto &b : entities->getChildren()) {
              checkLoopIterators(b);
            }
          }
          if (auto streams = decl->getPropertyValue("streams")) {
            for (const auto &s : streams->getChildren()) {
              checkLoopIterators(s);
            }
          }
          return;
        }
        if (node->getNodeType() == AST::Function) {
          auto func = std::static_pointer_cast<FunctionNode>(node);
          auto decl = ASTQuery::findDeclarationForNode(func, {}, tree);
          if (decl && decl->getEntityType() == "loop") {
            validateLoopDeclIterators(decl, func);
          }
          for (const auto &prop : func->getProperties()) {
            if (prop && prop->getValue()) {
              checkLoopIterators(prop->getValue());
            }
          }
          return;
        }
        if (node->getNodeType() == AST::Stream) {
          auto stream = std::static_pointer_cast<StreamNode>(node);
          checkLoopIterators(stream->getLeft());
          checkLoopIterators(stream->getRight());
          return;
        }
        for (const auto &child : node->getChildren()) {
          checkLoopIterators(child);
        }
      };
  checkLoopIterators(tree);
}

void CodeValidator::validateArraySizes(const ASTNode &node,
                                       std::vector<LangError> &errors,
                                       const ScopeStack &scope) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::ArrayDeclaration) {
    auto declaration = std::static_pointer_cast<DeclarationNode>(node);
    int size =
        ASTQuery::getEntityDeclaredSize(declaration, scope, m_tree, &errors);
    int datasize = getEntityDataSize(declaration, scope, &errors);
    if (size != datasize && datasize > 1) {
      LangError error;
      error.type = LangError::ArraySizeMismatch;
      node->copyLocationTo(error);
      error.errorTokens.push_back(
          declaration->getArrayIndex() ? declaration->getArrayIndex()->getName()
                                       : declaration->getName());
      error.errorTokens.push_back(std::to_string(size));
      error.errorTokens.push_back(std::to_string(datasize));
      errors.push_back(error);
    }
  }
  for (const ASTNode &child : node->getChildren()) {
    validateArraySizes(child, errors, scope);
  }
}

void CodeValidator::validateStreamSizes(const ASTNode &tree,
                                        std::vector<LangError> &errors,
                                        const ScopeStack &scope) {
  if (!tree) {
    return;
  }
  for (const ASTNode &node : tree->getChildren()) {
    if (!node) {
      continue;
    }
    if (node->getNodeType() == AST::Stream) {
      validateStreamInputSize(static_cast<StreamNode *>(node.get()), errors,
                              scope);
    } else if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "module" ||
          decl->getEntityType() == "reaction" ||
          decl->getEntityType() == "loop") {
        auto entitiesNode = decl->getPropertyValue("entities");
        auto streamsNode = decl->getPropertyValue("streams");
        ScopeStack subScope = scope;
        subScope.push_back(
            {decl, entitiesNode ? entitiesNode->getChildren()
                                : std::vector<ASTNode>{}});
        if (streamsNode) {
          for (const auto &streamChild : streamsNode->getChildren()) {
            if (streamChild && streamChild->getNodeType() == AST::Stream) {
              validateStreamInputSize(
                  static_cast<StreamNode *>(streamChild.get()), errors,
                  subScope);
            }
          }
        }
        if (entitiesNode) {
          validateStreamSizes(entitiesNode, errors, subScope);
        }
      }
    }
  }
}

void CodeValidator::validateRates(const ASTNode &tree) {
  if ((m_options & NO_RATE_VALIDATION) == 0 && tree) {
    for (const ASTNode &node : tree->getChildren()) {
      validateNodeRate(node, tree);
    }
  }
}

void CodeValidator::validateConstraints(const ASTNode &tree) {
  if (!tree) {
    return;
  }
  for (const ASTNode &node : tree->getChildren()) {
    if (node && node->getNodeType() == AST::Stream) {
      validateConstraintsForStream(std::static_pointer_cast<StreamNode>(node),
                                   ScopeStack(), tree);
    } else if (node && node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "module" ||
          decl->getEntityType() == "reaction" ||
          decl->getEntityType() == "loop") {
        auto entitiesNode = decl->getPropertyValue("entities");
        auto streamsNode = decl->getPropertyValue("streams");
        if (streamsNode) {
          ScopeStack subScope = {
              {decl, entitiesNode ? entitiesNode->getChildren()
                                  : std::vector<ASTNode>{}}};
          for (const auto &streamChild : streamsNode->getChildren()) {
            if (streamChild && streamChild->getNodeType() == AST::Stream) {
              validateConstraintsForStream(
                  std::static_pointer_cast<StreamNode>(streamChild), subScope,
                  tree);
            }
          }
        }
      }
    }
  }
}

void CodeValidator::validateNodeRate(const ASTNode &node, const ASTNode &tree) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() == "_domainDefinition" ||
        decl->getEntityType() == "gameDefinition") {
      for (const char *portListName : {"inputs", "outputs"}) {
        ASTNode portList = decl->getPropertyValue(portListName);
        if (portList && portList->getNodeType() == AST::List) {
          for (const ASTNode &portDecl : portList->getChildren()) {
            validateNodeRate(portDecl, tree);
          }
        }
      }
      return;
    }
    if (decl->getEntityType() == "signal" ||
        decl->getEntityType() == "switch") {
      ASTNode rateNode = decl->getPropertyValue("rate");
      bool isPropagated = decl->getCompilerProperty("ratePropagated") != nullptr;
      ASTNode domNode = CodeAnalysis::getNodeDomain(decl, {}, tree);
      std::shared_ptr<DeclarationNode> domDecl;
      if (domNode) {
        std::string domId = ASTQuery::getDomainIdentifier(domNode, {}, tree);
        if (!domId.empty()) {
          domDecl = ASTQuery::findDomainDeclaration(domId, tree);
        }
      }
      if (rateNode && rateNode->getNodeType() != AST::None) {
        double rate = CodeAnalysis::getNodeRate(decl, {}, tree);
        if (!isPropagated && rate <= 0) {
          LangError error;
          error.type = LangError::UnresolvedRate;
          (rateNode->getLine() > 0 ? rateNode : node)->copyLocationTo(error);
          error.errorTokens.push_back(decl->getName());
          m_errors.push_back(error);
        } else if (!isPropagated && rate > 0 && domDecl) {
          double domRate = ASTQuery::getDomainDefaultRate(domDecl);
          if (domRate <= 0) {
            LangError warning;
            warning.type = LangError::IgnoredRateInOneShotDomain;
            warning.severity = LangError::Warning;
            (rateNode->getLine() > 0 ? rateNode : node)
                ->copyLocationTo(warning);
            warning.errorTokens.push_back(decl->getName());
            warning.errorTokens.push_back(domDecl->getName());
            m_warnings.push_back(warning);
          }
        }
      }
    }
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    ASTNode domNode = CodeAnalysis::getNodeDomain(node, {}, tree);
    if (domNode) {
      std::string domId = ASTQuery::getDomainIdentifier(domNode, {}, tree);
      auto domDecl = !domId.empty()
                         ? ASTQuery::findDomainDeclaration(domId, tree)
                         : nullptr;
      if (domDecl && ASTQuery::getDomainDefaultRate(domDecl) > 0) {
        auto decl = ASTQuery::findDeclarationForNode(node, {}, tree);
        if (decl && (decl->getEntityType() == "signal" ||
                     decl->getEntityType() == "switch")) {
          double rate = CodeAnalysis::getNodeRate(node, {}, tree);
          if (rate <= 0) {
            LangError error;
            error.type = LangError::UnresolvedRate;
            node->copyLocationTo(error);
            error.errorTokens.push_back(ASTQuery::getNodeName(node));
            m_errors.push_back(error);
          }
        }
      }
    }
  } else if (node->getNodeType() == AST::Stream) {
    StreamNode *stream = static_cast<StreamNode *>(node.get());
    validateNodeRate(stream->getLeft(), tree);
    validateNodeRate(stream->getRight(), tree);
  } else if (node->getNodeType() == AST::Expression ||
             node->getNodeType() == AST::List) {
    for (const ASTNode &child : node->getChildren()) {
      validateNodeRate(child, tree);
    }
  } else if (node->getNodeType() == AST::Function) {
    for (const auto &prop :
         static_cast<FunctionNode *>(node.get())->getProperties()) {
      validateNodeRate(prop->getValue(), tree);
    }
  }
}

void CodeValidator::validateConstraintsForStream(
    std::shared_ptr<StreamNode> stream, const ScopeStack &scopeStack,
    const ASTNode &tree) {
  if (!stream) {
    return;
  }
  ASTNode node = stream->getLeft();
  while (node) {
    if (node->getNodeType() == AST::Function) {
      validateFunctionConstraints(std::static_pointer_cast<FunctionNode>(node),
                                  scopeStack, tree);
    }
    if (!stream || !stream->getRight()) {
      node = nullptr;
    } else if (stream->getRight()->getNodeType() == AST::Stream) {
      stream = std::static_pointer_cast<StreamNode>(stream->getRight());
      node = stream->getLeft();
    } else {
      node = stream->getRight();
      stream = nullptr;
    }
  }
}

void CodeValidator::validateFunctionConstraints(
    const std::shared_ptr<FunctionNode> &function, ScopeStack scopeStack,
    const ASTNode &tree) {
  auto declaration = ASTQuery::findDeclarationForNode(function, scopeStack, tree);
  if (!declaration) {
    return;
  }
  auto constraintsNode = declaration->getPropertyValue("constraints");
  auto entities = declaration->getPropertyValue("entities");
  scopeStack.push_back(
      {nullptr, entities ? entities->getChildren() : std::vector<ASTNode>{}});

  if (!constraintsNode) {
    return;
  }
  if (constraintsNode->getNodeType() == AST::Stream) {
    validateConstraintStream(
        std::static_pointer_cast<StreamNode>(constraintsNode), function,
        declaration, scopeStack, tree);
  } else if (constraintsNode->getNodeType() == AST::List) {
    for (const auto &constraint : constraintsNode->getChildren()) {
      if (constraint && constraint->getNodeType() == AST::Stream) {
        validateConstraintStream(
            std::static_pointer_cast<StreamNode>(constraint), function,
            declaration, scopeStack, tree);
      } else {
        std::cerr << "WARNING: Unexpected type in constraint, expecting stream"
                  << std::endl;
      }
    }
  } else {
    std::cerr << __FILE__ << ":" << __LINE__
              << " constraints must be streams or list of streams" << std::endl;
  }
}

void CodeValidator::validateConstraintStream(
    std::shared_ptr<StreamNode> stream,
    const std::shared_ptr<FunctionNode> &function,
    const std::shared_ptr<DeclarationNode> &declaration,
    const ScopeStack &scopeStack, const ASTNode &tree) {
  if (!stream) {
    return;
  }
  ASTNode node = stream->getLeft();
  std::vector<ASTNode> previous;
  while (node) {
    previous = resolveConstraintNode(node, previous, function, declaration,
                                     scopeStack, tree);
    if (!stream || !stream->getRight()) {
      node = nullptr;
    } else {
      node = stream->getRight();
      if (node->getNodeType() == AST::Stream) {
        stream = std::static_pointer_cast<StreamNode>(stream->getRight());
        node = stream->getLeft();
      } else {
        stream = nullptr;
      }
    }
  }
}

std::vector<ASTNode> CodeValidator::resolveConstraintNode(
    const ASTNode &node, const std::vector<ASTNode> &previous,
    const std::shared_ptr<FunctionNode> &function,
    const std::shared_ptr<DeclarationNode> &declaration,
    const ScopeStack &scopeStack, const ASTNode &tree) {
  if (!node) {
    return {};
  }
  if (node->getNodeType() == AST::MemberAccess) {
    auto pp = std::static_pointer_cast<MemberAccessNode>(node);
    if (pp->getPropertyName() == "size") {
      return {std::make_shared<ValueNode>(
          static_cast<int64_t>(CodeAnalysis::evaluateSizeMemberAccess(
              pp->getEntity(), scopeStack, declaration, function, tree)),
          __FILE__, __LINE__)};
    }
    if (pp->getPropertyName() == "rate") {
      return {std::make_shared<ValueNode>(
          CodeAnalysis::evaluateRateMemberAccess(pp->getEntity(), scopeStack,
                                                 declaration, function, tree),
          __FILE__, __LINE__)};
    }
    return {std::make_shared<AST>()};
  }
  if (node->getNodeType() == AST::Function) {
    StrideRuntimeStatus status;
    std::vector<ASTNode> output;
    output.emplace_back(std::make_shared<ValueNode>(true, __FILE__, __LINE__));
    auto functionInstance = std::static_pointer_cast<FunctionNode>(node);
    ASTRuntime::resolveFunction(functionInstance, previous, output, status);
    if (!status.ok) {
      LangError &err = status.err;
      err.filename = functionInstance->getFilename();
      err.lineNumber = functionInstance->getLine();
      if (err.errorTokens.empty()) {
        err.errorTokens.push_back(functionInstance->getName());
      } else {
        err.errorTokens[0] = functionInstance->getName();
      }
      m_errors.push_back(err);
    }
    return output;
  }
  if (node->getNodeType() == AST::Entity) {
    return {};
  }
  if (node->getNodeType() == AST::List) {
    std::vector<ASTNode> resolvedList;
    size_t i = 0;
    for (const auto &elem : node->getChildren()) {
      ASTNode previousNode = (i < previous.size()) ? previous[i] : nullptr;
      auto resolved = resolveConstraintNode(elem, {previousNode}, function,
                                            declaration, scopeStack, tree);
      resolvedList.insert(resolvedList.end(), resolved.begin(), resolved.end());
      ++i;
    }
    return resolvedList;
  }
  return {node};
}

void CodeValidator::sortDiagnostics() {
  auto compareDiag = [](const LangError &a, const LangError &b) {
    if (a.filename != b.filename) {
      return a.filename < b.filename;
    }
    if (a.lineNumber != b.lineNumber) {
      return a.lineNumber < b.lineNumber;
    }
    return a.columnNumber < b.columnNumber;
  };
  std::sort(m_errors.begin(), m_errors.end(), compareDiag);
  std::sort(m_warnings.begin(), m_warnings.end(), compareDiag);
}

void CodeValidator::validateStreamInputSize(StreamNode *stream,
                                            std::vector<LangError> &errors,
                                            const ScopeStack &scope) {
  if (!stream) {
    return;
  }
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();

  int leftOutSize =
      CodeAnalysis::getNodeNumOutputs(left, scope, m_tree, &errors);
  int rightInSize =
      CodeAnalysis::getNodeNumInputs(right, scope, m_tree, &errors);

  auto leftDecl = ASTQuery::findDeclarationForNode(left, scope, m_tree);
  ASTNode rightTarget =
      (right && right->getNodeType() == AST::Stream)
          ? static_cast<StreamNode *>(right.get())->getLeft()
          : right;
  auto rightDecl = ASTQuery::findDeclarationForNode(rightTarget, scope, m_tree);

  const bool involvesBuffer =
      (leftDecl && leftDecl->getEntityType() == "buffer") ||
      (rightDecl && rightDecl->getEntityType() == "buffer");

  if (!involvesBuffer &&
      leftOutSize != CodeAnalysis::SIZE_MEMBER_ACCESS &&
      rightInSize != CodeAnalysis::SIZE_MEMBER_ACCESS &&
      leftOutSize != CodeAnalysis::SIZE_UNKNOWN &&
      rightInSize != CodeAnalysis::SIZE_UNKNOWN) {
    const bool exactBroadcast =
        leftOutSize > 0 && rightInSize > 0 && (rightInSize % leftOutSize) == 0;
    if ((leftOutSize != rightInSize && !exactBroadcast) || rightInSize == 0) {
      LangError error;
      error.type = LangError::StreamMemberSizeMismatch;
      if (right) {
        right->copyLocationTo(error);
      }
      error.errorTokens.push_back(std::to_string(leftOutSize) + " ");
      error.errorTokens.push_back(ASTQuery::getNodeName(left));
      error.errorTokens.push_back(std::to_string(rightInSize));
      errors.push_back(error);
    } else if (left && left->getNodeType() == AST::Function &&
               leftOutSize == 1 && rightTarget &&
               rightTarget->getNodeType() == AST::Entity && rightDecl &&
               rightDecl->getNodeType() == AST::ArrayDeclaration &&
               rightInSize > 1) {
      LangError warning;
      warning.type = LangError::ScalarCallToArray;
      warning.severity = LangError::Warning;
      if (right) {
        right->copyLocationTo(warning);
      }
      warning.errorTokens.push_back(ASTQuery::getNodeName(left));
      warning.errorTokens.push_back(ASTQuery::getNodeName(rightTarget));
      warning.errorTokens.push_back(std::to_string(rightInSize));
      m_warnings.push_back(warning);
    }
  }

  if (right && right->getNodeType() == AST::Stream) {
    validateStreamInputSize(static_cast<StreamNode *>(right.get()), errors,
                            scope);
  }
}

int CodeValidator::getEntityDataSize(
    const std::shared_ptr<DeclarationNode> &declaration, const ScopeStack &scope,
    std::vector<LangError> *errors) {
  if (!declaration) {
    return 0;
  }
  const auto &ports = declaration->getProperties();
  if (ports.empty()) {
    return 0;
  }
  int size = CodeAnalysis::getNodeNumOutputs(ports.front()->getValue(), scope,
                                             m_tree, errors);
  for (const auto &port : ports) {
    int newSize = CodeAnalysis::getNodeNumOutputs(port->getValue(), scope,
                                                  m_tree, errors);
    if (size == 1 && newSize > 0) {
      size = newSize;
    }
  }
  return size;
}
