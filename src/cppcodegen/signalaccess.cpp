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

#include "stride/stridegen/cppcodegen/signalaccess.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/utils/astquery.h"

#include <algorithm>

using namespace strd;

namespace {

void collectDomainList(const ASTNode &domainsProp, const ASTNode &tree,
                       const std::map<std::string, std::string> &domainMap,
                       std::vector<std::string> &outDomains,
                       std::vector<std::string> &outFrameworks) {
  if (!domainsProp) {
    return;
  }
  for (const auto &domNode : domainsProp->getChildren()) {
    std::string domainId = ASTQuery::getDomainIdentifier(domNode, {}, tree);
    auto it = domainMap.find(domainId);
    if (it != domainMap.end()) {
      domainId = it->second;
    }
    if (std::find(outDomains.begin(), outDomains.end(), domainId) ==
        outDomains.end()) {
      outDomains.push_back(domainId);
      outFrameworks.push_back(ASTQuery::getFrameworkForDomain(domainId, tree));
    }
  }
}

bool isUnsynchronizedDirectAccess(const SignalAccess &access,
                                  const std::string &frameworkName,
                                  StrideSystem *system) {
  if (!system || frameworkName.empty()) {
    return false;
  }
  auto syncs = system->getFrameworkSynchronization(frameworkName);
  for (const auto &syncNode : syncs) {
    std::string readDom = ASTQuery::stringProperty(syncNode, "readDomain");
    std::string writeDom = ASTQuery::stringProperty(syncNode, "writeDomain");
    std::string scheme = ASTQuery::stringProperty(syncNode, "scheme");
    if (readDom.empty() && writeDom.empty() && scheme.empty()) {
      return true;
    }
    bool readMatches =
        readDom.empty() ||
        std::find(access.readDomains.begin(), access.readDomains.end(),
                  readDom) != access.readDomains.end();
    bool writeMatches =
        writeDom.empty() ||
        std::find(access.writeDomains.begin(), access.writeDomains.end(),
                  writeDom) != access.writeDomains.end();
    if (readMatches && writeMatches && scheme.empty()) {
      return true;
    }
  }
  return false;
}

} // namespace

SignalAccess SignalAccessAnalyzer::getSignalAccess(
    const ASTNode &instanceNode, const ASTNode &tree, StrideSystem *system,
    const std::map<std::string, std::string> &domainMap) {
  SignalAccess access;
  if (!instanceNode) {
    return access;
  }

  std::string frameworkName;
  if (auto frameworkNode = instanceNode->getCompilerProperty("framework");
      frameworkNode && frameworkNode->getNodeType() == AST::String) {
    frameworkName =
        std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
  }

  if (auto triggerResets = instanceNode->getCompilerProperty("resetDomains");
      triggerResets && !triggerResets->getChildren().empty()) {
    access.access = static_cast<DomainAccess>(access.access | ACCESS_SDRst);
  }

  if (instanceNode->getNodeType() != AST::Declaration &&
      instanceNode->getNodeType() != AST::ArrayDeclaration) {
    return access;
  }

  auto decl = std::static_pointer_cast<DeclarationNode>(instanceNode);
  collectDomainList(decl->getCompilerProperty("reads"), tree, domainMap,
                    access.readDomains, access.readFrameworks);
  collectDomainList(decl->getCompilerProperty("writes"), tree, domainMap,
                    access.writeDomains, access.writeFrameworks);

  bool crossesFrameworks = false;
  for (const auto &rf : access.readFrameworks) {
    for (const auto &wf : access.writeFrameworks) {
      if (rf != wf) {
        crossesFrameworks = true;
        access.access = static_cast<DomainAccess>(access.access |
                                                  ACCESS_CROSSES_FRAMEWORK);
        break;
      }
    }
    if (crossesFrameworks) {
      break;
    }
  }

  if (access.readDomains.size() == 1 && access.writeDomains.size() == 1 &&
      !crossesFrameworks && access.readDomains[0] == access.writeDomains[0]) {
    return access;
  }
  if (access.readDomains.empty() || access.writeDomains.empty()) {
    return access;
  }

  if (isUnsynchronizedDirectAccess(access, frameworkName, system)) {
    return access;
  }

  if (access.readDomains.size() > 1) {
    access.access = static_cast<DomainAccess>(access.access | ACCESS_MDR);
  } else if (access.readDomains.size() == 1) {
    access.access = static_cast<DomainAccess>(access.access | ACCESS_SDR);
  }
  if (access.writeDomains.size() > 1) {
    access.access = static_cast<DomainAccess>(access.access | ACCESS_MDW);
  } else if (access.writeDomains.size() == 1) {
    access.access = static_cast<DomainAccess>(access.access | ACCESS_SDW);
  }
  return access;
}
