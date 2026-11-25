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

#ifndef STRIDE_CPPCODEGEN_CODEENTITIES_HPP
#define STRIDE_CPPCODEGEN_CODEENTITIES_HPP

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/memberaccessnode.h"

namespace strd {
typedef enum {
  ACCESS_NONE = 0,
  ACCESS_SDR = 1 << 1,   // Single Domain Read
  ACCESS_SDW = 1 << 2,   // Single Domain Write
  ACCESS_MDR = 1 << 3,   // Multi Domain Read
  ACCESS_MDW = 1 << 4,   // Multi Domain Write
  ACCESS_SDRst = 1 << 5, // Single domain Reset
  ACCESS_MDRst = 1 << 6, // Multi domain Reset

  ACCESS_CROSSES_FRAMEWORK = 1 << 16
} DomainAccess;

struct SignalAccess {
  DomainAccess access = ACCESS_NONE;
  std::vector<std::string> readDomains;
  std::vector<std::string> readFrameworks;
  std::vector<std::string> writeDomains;
  std::vector<std::string> writeFrameworks;
};

enum class CodeEntityType { Declaration, Instance, Reset };

class CodeEntity {
public:
  virtual ~CodeEntity() = default;

  std::string name;
  CodeEntityType entityType{CodeEntityType::Declaration};
  std::vector<std::shared_ptr<CodeEntity>> dependents;
  std::vector<std::string> requiredIncludes;

  virtual std::string fullName() { return name; }
};

struct Declaration : public CodeEntity {
  Declaration() { entityType = CodeEntityType::Declaration; }
  std::string type;
  std::string code;
  std::string templateParams;
  std::string parent;
  std::vector<std::shared_ptr<Declaration>> dependsOn;
};

struct Instance : public CodeEntity {
  Instance(std::string name_, std::string prefix, uint64_t size,
           std::string type, std::vector<std::string> defaultValue,
           ASTNode instanceNode, SignalAccess access = SignalAccess())
      : prefix(std::move(prefix)), size(size), type(std::move(type)),
        defaultValue(std::move(defaultValue)),
        instanceNode(std::move(instanceNode)), access(std::move(access)) {
    entityType = CodeEntityType::Instance;
    name = std::move(name_);
  }

  std::string prefix;
  std::string parent;
  uint64_t size{0};
  std::string type;
  std::vector<std::string> defaultValue;
  ASTNode instanceNode;
  std::vector<std::string> constructorArgs;
  std::vector<std::string> templateArgs;
  SignalAccess access;

  std::string fullName() override { return prefix + name; }
};

struct Reset : public CodeEntity {
  explicit Reset(std::shared_ptr<DeclarationNode> decl)
      : resetEntityDecl(std::move(decl)) {
    entityType = CodeEntityType::Reset;
  }
  std::shared_ptr<DeclarationNode> resetEntityDecl;
};

struct DeclarationMap {
  std::shared_ptr<DeclarationNode> internalDecl;
  ASTNode externalConnection;
  std::vector<std::string> parentList;
  SignalAccess access;
  std::shared_ptr<MemberAccessNode> parentScopeProperty;
};

struct FunctionCall {
  std::shared_ptr<Instance> functionCall;
  std::shared_ptr<Declaration> declaration;
  std::vector<std::string> parentInstanceNames;
  std::map<std::string, std::vector<DeclarationMap>> entityInMap;
  std::map<std::string, std::vector<DeclarationMap>> entityOutMap;
  std::map<std::string, std::string> domainMap;
};

// C++ code generation container for a single domain.
class DomainCode {
public:
  std::vector<std::shared_ptr<CodeEntity>> scopeEntities;
  std::vector<std::shared_ptr<DeclarationNode>> globalReferences;

  std::vector<std::string> currentOutTokens;

  std::string initCode;
  std::vector<std::string> includeFiles;
  std::string processingCode;
  std::vector<std::string> postProcessingCode;
  std::string cleanupCode;

  std::vector<std::string> linkTargets;
  std::vector<std::string> linkDirs;
  std::vector<std::string> includeDirs;

  std::vector<DeclarationMap> inMap;
  std::vector<DeclarationMap> outMap;

  std::vector<FunctionCall> moduleCalls;

  std::map<std::shared_ptr<MemberAccessNode>, std::string> portPropertiesMap;

  void append(const DomainCode &newCode) { merge(newCode, false); }
  void prepend(const DomainCode &newCode) { merge(newCode, true); }

private:
  template <typename T>
  static void insertVector(std::vector<T> &dest, const std::vector<T> &src,
                           bool atFront) {
    dest.insert(atFront ? dest.begin() : dest.end(), src.begin(), src.end());
  }

  void merge(const DomainCode &other, bool atFront) {
    insertVector(scopeEntities, other.scopeEntities, atFront);
    if (atFront) {
      initCode = other.initCode + initCode;
      processingCode = other.processingCode + processingCode;
      cleanupCode = other.cleanupCode + cleanupCode;
    } else {
      initCode += other.initCode;
      processingCode += other.processingCode;
      cleanupCode += other.cleanupCode;
    }

    for (const auto &postCode : other.postProcessingCode) {
      if (std::find(postProcessingCode.begin(), postProcessingCode.end(),
                    postCode) == postProcessingCode.end()) {
        postProcessingCode.insert(atFront ? postProcessingCode.begin()
                                          : postProcessingCode.end(),
                                  postCode);
      }
    }

    insertVector(linkTargets, other.linkTargets, atFront);
    insertVector(linkDirs, other.linkDirs, atFront);
    insertVector(includeFiles, other.includeFiles, atFront);
    insertVector(includeDirs, other.includeDirs, atFront);
    insertVector(inMap, other.inMap, atFront);
    insertVector(outMap, other.outMap, atFront);
    insertVector(currentOutTokens, other.currentOutTokens, atFront);

    for (const auto &newReference : other.globalReferences) {
      if (!newReference) {
        continue;
      }
      bool registered = std::any_of(
          globalReferences.begin(), globalReferences.end(),
          [&](const auto &existingRef) {
            return existingRef &&
                   existingRef->getName() == newReference->getName();
          });
      if (!registered) {
        globalReferences.insert(atFront ? globalReferences.begin()
                                        : globalReferences.end(),
                                newReference);
      }
    }

    for (const auto &newEntry : other.portPropertiesMap) {
      if (!newEntry.first) {
        continue;
      }
      bool registered = std::any_of(
          portPropertiesMap.begin(), portPropertiesMap.end(),
          [&](const auto &entry) {
            return entry.first &&
                   entry.first->getEntity() == newEntry.first->getEntity() &&
                   entry.first->getPropertyName() ==
                       newEntry.first->getPropertyName();
          });
      if (!registered) {
        portPropertiesMap[newEntry.first] = newEntry.second;
      }
    }

    for (const auto &call : other.moduleCalls) {
      if (!call.functionCall) {
        continue;
      }
      bool found = std::any_of(
          moduleCalls.begin(), moduleCalls.end(), [&](const auto &existing) {
            return existing.functionCall &&
                   existing.functionCall->fullName() ==
                       call.functionCall->fullName();
          });
      if (!found) {
        moduleCalls.insert(atFront ? moduleCalls.begin() : moduleCalls.end(),
                           call);
      }
    }
  }
};

class DomainCodeMap : public std::map<std::string, DomainCode> {
public:
  void append(const DomainCodeMap &newMap) {
    for (const auto &[domainId, code] : newMap) {
      append(code, domainId);
    }
  }

  void append(const DomainCode &code, const std::string &domainId) {
    (*this)[domainId].append(code);
  }

  void prepend(const DomainCode &code, const std::string &domainId) {
    (*this)[domainId].prepend(code);
  }
};
} // namespace strd

#endif // STRIDE_CPPCODEGEN_CODEENTITIES_HPP
