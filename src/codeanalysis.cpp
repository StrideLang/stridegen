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

#include "stride/stridegen/codeanalysis.hpp"

#include "stride/parser/generatorrangenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <set>

using namespace strd;

std::vector<std::string> CodeAnalysis::getUsedDomains(ASTNode tree) {
  std::vector<std::string> domains;
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      StreamNodeIterator it(std::static_pointer_cast<StreamNode>(node));
      while (it.hasNext()) {
        ASTNode childNode = it.next();
        std::string domainName =
            CodeAnalysis::getNodeDomainName(childNode, {}, tree);
        if (domainName.size() > 0) {
          if (std::find(domains.begin(), domains.end(), domainName) ==
              domains.end()) {
            domains.push_back(domainName);
          }
        }
      }
    } else if (node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      auto domainName =
          CodeAnalysis::getNodeDomainName(decl, ScopeStack(), tree);
      if (domainName.size() > 0) {
        if (std::find(domains.begin(), domains.end(), domainName) ==
            domains.end()) {
          domains.push_back(domainName);
        }
      }
    }
  }
  return domains;
}

std::vector<std::string> CodeAnalysis::getUsedFrameworks(ASTNode tree) {
  std::vector<std::string> domains = CodeAnalysis::getUsedDomains(tree);
  std::vector<std::string> usedFrameworks;
  for (const std::string &domain : domains) {
    usedFrameworks.push_back(CodeAnalysis::getFrameworkForDomain(domain, tree));
  }
  return usedFrameworks;
}

ASTNode CodeAnalysis::getInstance(ASTNode block, ScopeStack scopeStack,
                                  ASTNode tree) {
  if (!block) {
    return nullptr;
  }
  ASTNode inst;
  if (block->getNodeType() == AST::List) {
    inst = block;
  } else if (block->getNodeType() == AST::Function) {
    inst = block;
  } else if (block->getNodeType() == AST::Declaration ||
             block->getNodeType() == AST::ArrayDeclaration) {
    if (std::static_pointer_cast<DeclarationNode>(block)->getEntityType() ==
            "signal" ||
        std::static_pointer_cast<DeclarationNode>(block)->getEntityType() ==
            "switch" ||
        std::static_pointer_cast<DeclarationNode>(block)->getEntityType() ==
            "trigger" ||
        std::static_pointer_cast<DeclarationNode>(block)->getEntityType() ==
            "string" ||
        ASTQuery::isConstant(std::static_pointer_cast<DeclarationNode>(block),
                             scopeStack, tree)) {
      inst = block;
    } else if (std::static_pointer_cast<DeclarationNode>(block)
                   ->getEntityType() == "reaction") {
      inst = block;
    } else {
      std::cout << __FILE__ << ":" << __LINE__
                << " Unexpected declaration in getInstance()";
    }
  } else if (block->getNodeType() == AST::Int ||
             block->getNodeType() == AST::Real ||
             block->getNodeType() == AST::String) {
    inst = block;
  } else {
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(
            block->getCompilerProperty("declaration"));
    if (!decl) {
      decl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(block),
                                             scopeStack, tree);
    }
    if (decl) {
      auto typeDecl = ASTQuery::findTypeDeclaration(decl, scopeStack, tree);
      if (typeDecl && typeDecl->getEntityType() == "platformBlock") {
        inst = block;
      } else {
        inst = decl;
      }
    } else {
      inst = block;
    }
  }
  return inst;
}

std::shared_ptr<DeclarationNode> CodeAnalysis::getDeclaration(ASTNode node) {
  if (node->getNodeType() == AST::Int || node->getNodeType() == AST::Real ||
      node->getNodeType() == AST::String) {
    return nullptr;
  }
  return std::static_pointer_cast<DeclarationNode>(
      node->getCompilerProperty("declaration"));
}

std::shared_ptr<DeclarationNode>
CodeAnalysis::findDomainDeclaration(std::string domainName,
                                    std::string framework, ASTNode tree) {
  std::string domainFramework;
  auto separatorIndex = domainName.find("::");
  if (separatorIndex != std::string::npos) {
    domainFramework = domainName.substr(0, separatorIndex);
    domainName = domainName.substr(separatorIndex + 2);
  }

  if (domainFramework != framework && domainFramework != "") {
    std::cerr << __FILE__ << ":" << __LINE__ << "Unexpected domain mismatch"
              << std::endl;
  }
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      std::shared_ptr<DeclarationNode> decl =
          std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "_domainDefinition" ||
          decl->getEntityType() == "gameDefinition") {
        if (domainName == decl->getName()) {
          auto domainDeclFramework = decl->getCompilerProperty("framework");
          if (domainDeclFramework &&
              domainDeclFramework->getNodeType() == AST::String) {
            if (framework ==
                std::static_pointer_cast<ValueNode>(domainDeclFramework)
                    ->getStringValue()) {
              return decl;
            }
          }
          if (framework.size() == 0) {
            return decl;
          }
        }
      }
    }
  }
  return nullptr;
}

std::shared_ptr<DeclarationNode>
CodeAnalysis::findDomainDeclaration(std::string domainId, ASTNode tree) {
  std::string domainFramework;

  auto separatorIndex = domainId.find("::");
  if (separatorIndex != std::string::npos) {
    domainFramework = domainId.substr(0, separatorIndex);
    if (domainFramework !=
        CodeAnalysis::getFrameworkForDomain(domainId, tree)) {
      std::cerr << __FILE__ << ":" << __LINE__ << "ERROR framework mismatch"
                << std::endl;
    }
    domainId = domainId.substr(separatorIndex + 2);
  }
  auto trimmedDomain = domainId.substr(0, domainId.find(':'));
  return findDomainDeclaration(trimmedDomain, domainFramework, tree);
}

ASTNode CodeAnalysis::getNodeDomain(ASTNode node, ScopeStack scopeStack,
                                    ASTNode tree) {
  ASTNode domainNode = nullptr;
  if (!node) {
    return nullptr;
  }

  if (node->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration;
    auto declProp = node->getCompilerProperty("declaration");
    if (declProp && (declProp->getNodeType() == AST::Declaration ||
                     declProp->getNodeType() == AST::ArrayDeclaration)) {
      declaration = std::static_pointer_cast<DeclarationNode>(declProp);
    } else {
      declaration = ASTQuery::findDeclarationByName(
          name->getName(), scopeStack, tree, name->getNamespaceList());
    }

    if (declaration) {
      auto typeDeclaration =
          ASTQuery::findTypeDeclaration(declaration, scopeStack, tree);
      if (typeDeclaration &&
          typeDeclaration->getEntityType() == "platformModule") {
        domainNode = name->getCompilerProperty("domain")->deepCopy();
        domainNode->setNamespaceList(node->getNamespaceList());
      } else {
        if (declaration->getDomain()) {
          domainNode = declaration->getDomain()->deepCopy();
          if (node->getNamespaceList().size() > 0) {
            domainNode->setNamespaceList(node->getNamespaceList());
          }
        }
      }
    }
  } else if (node->getNodeType() == AST::Array) {
    ArrayNode *name = static_cast<ArrayNode *>(node.get());

    std::shared_ptr<DeclarationNode> declaration;
    auto declProp = node->getCompilerProperty("declaration");
    if (declProp && (declProp->getNodeType() == AST::Declaration ||
                     declProp->getNodeType() == AST::ArrayDeclaration)) {
      declaration = std::static_pointer_cast<DeclarationNode>(declProp);
    } else {
      declaration = ASTQuery::findDeclarationByName(
          name->getName(), scopeStack, tree, name->getNamespaceList());
    }
    if (declaration && declaration->getDomain()) {
      domainNode = declaration->getDomain()->deepCopy();
      domainNode->setNamespaceList(node->getNamespaceList());
    }
  } else if (node->getNodeType() == AST::List) {
    std::vector<std::string> domainList;
    std::string tempDomainName;
    for (const ASTNode &member : node->getChildren()) {
      if (member->getNodeType() == AST::Entity) {
        EntityNode *name = static_cast<EntityNode *>(member.get());
        std::shared_ptr<DeclarationNode> declaration;
        auto declProp = member->getCompilerProperty("declaration");
        if (declProp && (declProp->getNodeType() == AST::Declaration ||
                         declProp->getNodeType() == AST::ArrayDeclaration)) {
          declaration = std::static_pointer_cast<DeclarationNode>(declProp);
        } else {
          declaration = ASTQuery::findDeclarationByName(
              name->getName(), scopeStack, tree, name->getNamespaceList());
        }
        if (declaration) {
          tempDomainName =
              CodeAnalysis::getNodeDomainName(declaration, scopeStack, tree);
        }
      } else if (member->getNodeType() == AST::Array) {
        ArrayNode *name = static_cast<ArrayNode *>(member.get());
        std::shared_ptr<DeclarationNode> declaration;
        auto declProp = member->getCompilerProperty("declaration");
        if (declProp && (declProp->getNodeType() == AST::Declaration ||
                         declProp->getNodeType() == AST::ArrayDeclaration)) {
          declaration = std::static_pointer_cast<DeclarationNode>(declProp);
        } else {
          declaration = ASTQuery::findDeclarationByName(name->getName(),
                                                        scopeStack, tree);
        }
        if (declaration) {
          tempDomainName =
              CodeAnalysis::getNodeDomainName(declaration, scopeStack, tree);
        }
      } else if (member->getNodeType() == AST::Int ||
                 member->getNodeType() == AST::Real ||
                 member->getNodeType() == AST::String ||
                 member->getNodeType() == AST::Switch ||
                 member->getNodeType() == AST::MemberAccess) {
        continue; // Don't append empty domain to domainList, as a value
                  // should take any domain.
      } else {
        tempDomainName =
            CodeAnalysis::getNodeDomainName(member, scopeStack, tree);
      }
      domainList.push_back(tempDomainName);
    }
    // There is no real resolution for domain from a list. The caller
    // should query each member for the domain. For now, assuming the first
    // element of the list determines domain.
    return CodeAnalysis::getNodeDomain(node->getChildren().at(0), scopeStack,
                                       tree);
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = static_cast<DeclarationNode *>(node.get());
    if (decl->getEntityType() == "reaction") {
      decl->getCompilerProperty("triggerDomain");
      // FIXME implement domain support for reactions
    } else {
      if (decl->getDomain()) {
        domainNode = decl->getDomain()->deepCopy();
        auto frameworkNode = node->getCompilerProperty("framework");
        if (frameworkNode) {
          domainNode->addScope(
              std::static_pointer_cast<ValueNode>(frameworkNode)
                  ->getStringValue());
        }
        //        domainNode->setNamespaceList(node->getNamespaceList());
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    domainNode = node->getCompilerProperty(
        "domain"); // static_cast<FunctionNode *>(node.get())->getDomain();
    if (!domainNode) {
      auto funcDecl = ASTQuery::findDeclarationByName(
          std::static_pointer_cast<FunctionNode>(node)->getName(), scopeStack,
          tree, node->getNamespaceList());
      if (funcDecl) {
        if (funcDecl->getEntityType() == "platformModule") {
          domainNode = funcDecl->getPropertyValue("domain");

          // TODO move this namespace resolution for domains to be more general
          auto domainId =
              CodeAnalysis::getDomainIdentifier(domainNode, {}, tree);
          std::string domainFramework;
          auto separatorIndex = domainId.find("::");
          if (separatorIndex != std::string::npos) {
            domainFramework = domainId.substr(0, separatorIndex);
            domainId = domainId.substr(separatorIndex + 2);
          }

          auto domainDecl = CodeAnalysis::findDomainDeclaration(
              domainId, domainFramework, tree);
          if (domainDecl) {
            auto parentDomain = domainDecl->getPropertyValue("parentDomain");
            if (parentDomain && parentDomain->getNodeType() == AST::Entity) {
              auto domainInstanceCountNode =
                  parentDomain->getCompilerProperty("instances");
              if (!domainInstanceCountNode) {
                domainInstanceCountNode =
                    std::make_shared<ValueNode>((int64_t)0, __FILE__, __LINE__);
                parentDomain->setCompilerProperty("instances",
                                                  domainInstanceCountNode);
              }
            }
          }
        } else if (funcDecl->getEntityType() == "reaction") {
          // reactions currently belong to a single domain.
          domainNode = funcDecl->getPropertyValue("domain");
        }
      }
    }
  } else if (node->getNodeType() == AST::Expression) {
    ExpressionNode *expr = static_cast<ExpressionNode *>(node.get());
    if (expr->isUnary()) {
      domainNode =
          CodeAnalysis::getNodeDomain(expr->getValue(), scopeStack, tree);
    } else {
      if (expr->getCompilerProperty("samplingDomain")) {
        return expr->getCompilerProperty("samplingDomain");
      }
    }
  } else if (node->getNodeType() == AST::Stream) {
    StreamNode *stream = static_cast<StreamNode *>(node.get());
    ASTNode leftDomain = getNodeDomain(stream->getLeft(), scopeStack, tree);
    ASTNode rightDomain = getNodeDomain(stream->getRight(), scopeStack, tree);
    if (!leftDomain) {
      leftDomain = rightDomain;
    } else if (!rightDomain) {
      rightDomain = leftDomain;
    }
    if ((leftDomain && rightDomain) &&
        (getNodeDomainName(leftDomain, scopeStack, tree) ==
         getNodeDomainName(rightDomain, scopeStack, tree))) {
      return leftDomain;
    }
  } else if (node->getNodeType() == AST::Real ||
             node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::Switch ||
             node->getNodeType() == AST::String) {
    domainNode = std::static_pointer_cast<ValueNode>(node)->getDomain();
  } else if (node->getNodeType() == AST::MemberAccess) {

    domainNode = node->getCompilerProperty("domain");
  }

  return domainNode;
}

std::string CodeAnalysis::getNodeDomainName(ASTNode node, ScopeStack scopeStack,
                                            ASTNode tree) {
  std::string domainName;
  ASTNode domainNode = CodeAnalysis::getNodeDomain(node, scopeStack, tree);
  if (domainNode) {
    domainName =
        CodeAnalysis::getDomainIdentifier(domainNode, scopeStack, tree);
  }
  return domainName;
}

std::string CodeAnalysis::getDomainIdentifier(ASTNode domain,
                                              ScopeStack scopeStack,
                                              ASTNode tree) {
  std::string name;
  // To avoid inconsistencies, we rely on the block name rather than the
  // domainName property. This guarantees uniqueness within a scope.
  // TODO we should add scope markers to this identifier to avoid clashes
  if (domain) {
    if (domain->getNodeType() == AST::Entity) {
      auto domainBlock = std::static_pointer_cast<EntityNode>(domain);
      std::string framework;
      if (domainBlock->getScopeLevels() > 0) {
        framework = domainBlock->getNamespaceList()[0];
      }
      std::shared_ptr<DeclarationNode> domainDeclaration =
          CodeAnalysis::findDomainDeclaration(domainBlock->getName(), framework,
                                              tree);
      if (domainDeclaration) {
        if (domainDeclaration->getEntityType() == "_domainDefinition" ||
            domainDeclaration->getEntityType() == "gameDefinition") {
          name = domainDeclaration->getName();
        } else if (domainDeclaration->getEntityType() == "PlatformDomain") {
          auto domainNameNode = domainDeclaration->getPropertyValue("value");
          name = getDomainIdentifier(domainNameNode, scopeStack, tree);
        }
        auto frameworkNode =
            domainDeclaration->getCompilerProperty("framework");
        std::string frameworkName;
        if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
          frameworkName = std::static_pointer_cast<ValueNode>(frameworkNode)
                              ->getStringValue();
          if (frameworkName.size() > 0) {
            name = frameworkName + "::" + name;
          }
        }
      }
      auto domainInstanceIndex = domain->getCompilerProperty("domainInstance");
      if (domainInstanceIndex) {
        int index = std::static_pointer_cast<ValueNode>(domainInstanceIndex)
                        ->getIntValue();
        name += ":" + std::to_string(index);
      }
    } else if (domain->getNodeType() == AST::Array) {
      auto domainBlock = std::static_pointer_cast<ArrayNode>(domain);
      auto domainDeclaration = ASTQuery::findDeclarationByName(
          domainBlock->getName(), scopeStack, tree);
      if (domainDeclaration) {
        if (domainDeclaration->getEntityType() == "_domainDefinition" ||
            domainDeclaration->getEntityType() == "gameDefinition") {
          name = domainDeclaration->getName();
        } else if (domainDeclaration->getEntityType() == "PlatformDomain") {
          auto domainNameNode = domainDeclaration->getPropertyValue("value");
          name = getDomainIdentifier(domainNameNode, scopeStack, tree);
        }
      }
    } else if (domain->getNodeType() == AST::String) {
      // Should anything be added to the id? Scope?
      name = std::static_pointer_cast<ValueNode>(domain)->getStringValue();
      std::cerr << "DEPRECATED: String domain no longer allowed." << std::endl;
      //      assert(0 == 1); // Should not be allowed
    } else if (domain->getNodeType() == AST::MemberAccess) {
      auto memberAccess = std::static_pointer_cast<MemberAccessNode>(domain);
      name = memberAccess->getEntity() + "_" + memberAccess->getPropertyName();
    }
  }
  return name;
}

double CodeAnalysis::getNodeRate(ASTNode node, ScopeStack scope, ASTNode tree) {
  if (node->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(name->getName(), scope, tree,
                                        name->getNamespaceList());
    if (declaration) {
      ASTNode rateNode = declaration->getPropertyValue("rate");
      if (rateNode) {
        std::vector<LangError> tempErrors;
        auto rate = CodeAnalysis::resolveRateToFloat(rateNode, scope, tree,
                                                     &tempErrors);
        if (tempErrors.size() == 0) {
          return rate;
        } else {
          return -1;
        }
      }
    }
  } else if (node->getNodeType() == AST::Array) {
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(arrayNode->getName(), scope, tree,
                                        arrayNode->getNamespaceList());
    if (declaration) {
      ASTNode rateNode = declaration->getPropertyValue("rate");
      if (rateNode) {
        std::vector<LangError> tempErrors;
        auto rate = CodeAnalysis::resolveRateToFloat(rateNode, scope, tree,
                                                     &tempErrors);
        if (tempErrors.size() == 0) {
          return rate;
        } else {
          return -1;
        }
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    FunctionNode *func = static_cast<FunctionNode *>(node.get());
    return func->getRate();
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    double rate = -1.0;
    for (const ASTNode &element : node->getChildren()) {
      double elementRate = CodeAnalysis::getNodeRate(element, scope, tree);
      if (elementRate != -1.0) {
        if (rate != elementRate) {
          //                    qDebug() << "Warning: List has different
          //                    rates!";
        }
        rate = elementRate;
      }
    }
    return rate;
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl) {
      ASTNode rateNode = decl->getPropertyValue("rate");
      if (rateNode) {
        std::vector<LangError> tempErrors;
        auto rate = CodeAnalysis::resolveRateToFloat(rateNode, scope, tree,
                                                     &tempErrors);
        if (tempErrors.size() == 0) {
          return rate;
        } else {
          return -1;
        }
      }
    }
  }
  return -1;
}

double CodeAnalysis::resolveRateToFloat(ASTNode rateNode, ScopeStack scope,
                                        ASTNode tree,
                                        std::vector<LangError> *errors) {
  double rate = -1;

  if (rateNode->getNodeType() == AST::Int) {
    rate = ASTFunctions::evaluateConstInteger(rateNode, {}, tree, errors);
    return rate;
  } else if (rateNode->getNodeType() == AST::Real) {
    rate = ASTFunctions::evaluateConstReal(rateNode, {}, tree, errors);
    return rate;
  } else if (rateNode->getNodeType() == AST::Entity) {
    EntityNode *entity = static_cast<EntityNode *>(rateNode.get());
    std::shared_ptr<DeclarationNode> valueDeclaration =
        ASTQuery::findDeclarationByName(entity->getName(), scope, tree,
                                        entity->getNamespaceList());
    if (valueDeclaration &&
        ASTQuery::isConstant(valueDeclaration, scope, tree)) {
      std::shared_ptr<PropertyNode> property = ASTQuery::findPropertyByName(
          valueDeclaration->getProperties(), "value");
      if (property) {
        std::shared_ptr<ValueNode> rateNode =
            std::static_pointer_cast<ValueNode>(property->getValue());
        if (rateNode->getNodeType() == AST::Int) {
          rate = ASTFunctions::evaluateConstInteger(rateNode, {}, tree, errors);
        } else if (rateNode->getNodeType() == AST::Real) {
          rate = ASTFunctions::evaluateConstReal(rateNode, {}, tree, errors);
        }
      }
    }
    if (errors->size() == 0) {
      return rate;
    }
  } else if (rateNode->getNodeType() == AST::MemberAccess) {
  }
  return rate;
}

double CodeAnalysis::getDomainDefaultRate(
    std::shared_ptr<DeclarationNode> domainDecl) {
  assert(domainDecl->getEntityType() == "_domainDefinition" ||
         domainDecl->getEntityType() == "gameDefinition");
  auto ratePort = domainDecl->getPropertyValue("rate");
  if (ratePort && (ratePort->getNodeType() == AST::Real ||
                   ratePort->getNodeType() == AST::Int)) {
    double rate = std::static_pointer_cast<ValueNode>(ratePort)->toReal();
    return rate;
  }
  return -1;
}

std::string CodeAnalysis::getFrameworkForDomain(std::string domainName,
                                                ASTNode tree) {
  std::string domainFramework;
  auto separatorIndex = domainName.find("::");
  if (separatorIndex != std::string::npos) {
    domainFramework = domainName.substr(0, separatorIndex);
    domainName = domainName.substr(separatorIndex + 2);
  }
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      DeclarationNode *decl = static_cast<DeclarationNode *>(node.get());
      if (decl->getEntityType() == "_domainDefinition" ||
          decl->getEntityType() == "gameDefinition") {
        std::string declDomainName = decl->getName();
        if (declDomainName == domainName) {
          ASTNode frameworkNameValue = decl->getPropertyValue("framework");
          if (frameworkNameValue->getNodeType() == AST::String) {
            return static_cast<ValueNode *>(frameworkNameValue.get())
                ->getStringValue();
          } else if (frameworkNameValue->getNodeType() == AST::Entity) {
            auto fwBlock =
                std::static_pointer_cast<EntityNode>(frameworkNameValue);

            auto declImportFramework = decl->getCompilerProperty("framework");
            std::string frameworkName;
            if (declImportFramework) {
              frameworkName =
                  std::static_pointer_cast<ValueNode>(declImportFramework)
                      ->getStringValue();
            }
            std::shared_ptr<DeclarationNode> fwDeclaration =
                ASTQuery::findDeclarationByName(fwBlock->getName(), {}, tree,
                                                fwBlock->getNamespaceList(),
                                                frameworkName);
            if (fwDeclaration &&
                (fwDeclaration->getEntityType() == "_frameworkDescription")) {
              ASTNode frameworkName =
                  fwDeclaration->getPropertyValue("frameworkName");
              if (frameworkName &&
                  frameworkName->getNodeType() == AST::String) {
                if (domainFramework !=
                    static_cast<ValueNode *>(frameworkName.get())
                        ->getStringValue()) {
                  //                    qDebug() << "Inconsistent name";
                  continue;
                }
                return static_cast<ValueNode *>(frameworkName.get())
                    ->getStringValue();
              }
            }
          }
        }
      }
    }
  }
  return std::string();
}

int CodeAnalysis::numParallelStreams(StreamNode *stream, StrideSystem &platform,
                                     const ScopeStack &scope, ASTNode tree,
                                     std::vector<LangError> *errors) {
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  int numParallel = 0;

  int leftSize = 0;
  int rightSize = 0;

  if (left->getNodeType() == AST::Entity) {
    leftSize = ASTQuery::getNodeSize(left, scope, tree);
  } else if (left->getNodeType() == AST::List) {
    leftSize = ASTQuery::getNodeSize(left, scope, tree);
  } else {
    leftSize = CodeAnalysis::getNodeNumOutputs(left, scope, tree, errors);
  }
  if (right->getNodeType() == AST::Entity ||
      right->getNodeType() == AST::List) {
    rightSize = ASTQuery::getNodeSize(right, scope, tree);
  } else if (right->getNodeType() == AST::Function) {
    int functionNodeSize = ASTQuery::getNodeSize(right, scope, tree);
    if (functionNodeSize == 1) {
      rightSize = leftSize;
    }
  } else if (right->getNodeType() == AST::Stream) {
    StreamNode *rightStream = static_cast<StreamNode *>(right.get());
    numParallel =
        numParallelStreams(rightStream, platform, scope, tree, errors);

    ASTNode firstMember = rightStream->getLeft();
    if (firstMember->getNodeType() == AST::Entity ||
        firstMember->getNodeType() == AST::List) {
      rightSize = ASTQuery::getNodeSize(firstMember, scope, tree);
    } else {
      rightSize =
          CodeAnalysis::getNodeNumInputs(firstMember, scope, tree, errors);
    }
    if (firstMember->getNodeType() == AST::Function) {
      int functionNodeSize = ASTQuery::getNodeSize(firstMember, scope, tree);
      if (functionNodeSize == 1) {
        rightSize =
            CodeAnalysis::getNodeNumInputs(firstMember, scope, tree, errors);
        ;
      } else {
      }
    }
  } else {
    rightSize = CodeAnalysis::getNodeNumInputs(right, scope, tree, errors);
  }
  int thisParallel = -1;

  if (leftSize == rightSize ||
      (rightSize / (float)leftSize) == (int)(rightSize / (float)leftSize)) {
    if (leftSize == 0) {
      thisParallel = -1;
    }
    if (leftSize == rightSize) {
      thisParallel = leftSize;
    } else {
      thisParallel = leftSize / rightSize;
    }
  }
  if (leftSize == 1) {
    thisParallel = rightSize;
  } else if (rightSize == 1) {
    thisParallel = leftSize;
  }
  if (thisParallel != numParallel && numParallel > 0) {
    if (rightSize == 1)
      numParallel = -1;
  } else {
    numParallel = thisParallel;
  }

  return numParallel;
}

static std::shared_ptr<DeclarationNode>
resolveConnectionEntityInternal(ASTNode node, ScopeStack scopeStack,
                                ASTNode tree, bool downStream,
                                std::set<const void *> &visited) {
  if (!node || visited.count(node.get())) {
    return nullptr;
  }
  visited.insert(node.get());

  if (node->getNodeType() == AST::Declaration) { // Signal
    return std::static_pointer_cast<DeclarationNode>(node);
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    return ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node),
                                           scopeStack, tree);
  } else if (node->getNodeType() == AST::Function) {
    auto funcDecl = std::static_pointer_cast<DeclarationNode>(
        node->getCompilerProperty("declaration"));
    if (funcDecl) {
      auto ports = funcDecl->getPropertyValue("ports");
      auto portDeclBlock = downStream
                               ? ASTQuery::getModuleMainOutputPort(funcDecl)
                               : ASTQuery::getModuleMainInputPort(funcDecl);
      if (portDeclBlock) {
        auto portBlock = portDeclBlock->getPropertyValue("block");
        if (portBlock) {
          auto funcName = funcDecl->getName();
          auto portBlockDecl = ASTQuery::findDeclarationByName(
              ASTQuery::getNodeName(portBlock),
              {{node, std::static_pointer_cast<DeclarationNode>(funcDecl)
                          ->getPropertyValue("blocks")
                          ->getChildren()}},
              nullptr);
          if (portBlockDecl) {
            auto blockDomain =
                CodeAnalysis::getNodeDomain(portBlockDecl, {}, nullptr);
            if (blockDomain &&
                blockDomain->getNodeType() == AST::MemberAccess) {
              auto portName =
                  std::static_pointer_cast<MemberAccessNode>(blockDomain)
                      ->getEntity();
              assert(std::static_pointer_cast<MemberAccessNode>(blockDomain)
                         ->getPropertyName() == "domain");
              // Now match the port name from the domain to the port
              // declaration
              for (const auto &port : ports->getChildren()) {
                if (port->getNodeType() == AST::Declaration) {
                  auto portDecl =
                      std::static_pointer_cast<DeclarationNode>(port);
                  if (portDecl->getName() ==
                      portName) { // Port match. Now get outer node
                    auto portType =
                        std::static_pointer_cast<DeclarationNode>(port)
                            ->getEntityType();
                    // TODO connect all port types
                    if (portType == "mainOutputPort" && downStream) {
                      auto outerBlock =
                          node->getCompilerProperty("outputBlock");
                      if (outerBlock) {
                        if (outerBlock->getNodeType() == AST::Entity ||
                            outerBlock->getNodeType() == AST::Array) {
                          auto decl = ASTQuery::findDeclarationByName(
                              ASTQuery::getNodeName(outerBlock), scopeStack,
                              tree);
                          if (decl) {
                            return decl;
                          } else {
                            return std::shared_ptr<DeclarationNode>();
                          }
                        }
                        return resolveConnectionEntityInternal(
                            outerBlock, scopeStack, tree, true, visited);
                      }
                    } else if (portType == "mainInputPort" && !downStream) {
                      auto outerBlock = node->getCompilerProperty("inputBlock");
                      if (outerBlock) {
                        if (outerBlock->getNodeType() == AST::Entity ||
                            outerBlock->getNodeType() == AST::Array) {
                          auto decl = ASTQuery::findDeclarationByName(
                              ASTQuery::getNodeName(outerBlock), scopeStack,
                              tree);
                          if (decl) {
                            return decl;
                          } else {
                            return std::shared_ptr<DeclarationNode>();
                          }
                        }
                        return resolveConnectionEntityInternal(
                            outerBlock, scopeStack, tree, false, visited);
                      }
                    }
                  }
                }
              }
            } else {
              std::cerr << "Unexpected port block domain" << std::endl;
            }
          }
        }
      }
    }
  }
  return std::shared_ptr<DeclarationNode>();
}

std::shared_ptr<DeclarationNode>
CodeAnalysis::resolveConnectionEntity(ASTNode node, ScopeStack scopeStack,
                                      ASTNode tree, bool downStream) {
  std::set<const void *> visited;
  return resolveConnectionEntityInternal(node, scopeStack, tree, downStream,
                                         visited);
}

ASTNode CodeAnalysis::getMatchedOuterInstance(
    std::shared_ptr<FunctionNode> functionNode,
    std::shared_ptr<DeclarationNode> blockDecl,
    std::shared_ptr<DeclarationNode> funcDecl, ScopeStack scopeStack,
    ASTNode tree) {
  // Instance is declaration for entity, but stream node for function
  ASTNode matchedInst = nullptr;
  auto internalBlocks = funcDecl->getPropertyValue("blocks")->getChildren();
  for (const auto &port : funcDecl->getPropertyValue("ports")->getChildren()) {
    if (port->getNodeType() == AST::Declaration) {
      auto portDecl = std::static_pointer_cast<DeclarationNode>(port);

      auto portBlock = portDecl->getPropertyValue("block");

      // Search only within internal scope for port entity
      auto portBlockDecl = ASTQuery::findDeclarationByName(
          ASTQuery::getNodeName(portBlock), {{functionNode, internalBlocks}},
          nullptr);

      if (portBlockDecl && portBlockDecl->getName() == blockDecl->getName()) {
        // If it's a main port, we can get the external entity from the
        // compiler properties of the FunctionNode
        if (portDecl->getEntityType() == "mainOutputPort") {
          ASTNode extOutputBlockInst;
          auto outputBlock = functionNode->getCompilerProperty("outputBlock");
          if (outputBlock) {
            extOutputBlockInst =
                CodeAnalysis::getInstance(outputBlock, scopeStack, tree);
          }
          matchedInst = extOutputBlockInst;
          break;
        } else if (portDecl->getEntityType() == "mainInputPort") {
          ASTNode extInputBlockInst;
          auto inputBlock = functionNode->getCompilerProperty("inputBlock");
          if (inputBlock) {
            extInputBlockInst =
                CodeAnalysis::getInstance(inputBlock, scopeStack, tree);
          }
          matchedInst = extInputBlockInst;
          break;
        } else {
          // If it's a secondary port, we get the entity from the FunctionNode
          // properties
          auto propertyName = std::static_pointer_cast<ValueNode>(
              portDecl->getPropertyValue("name"));
          if (propertyName->getNodeType() == AST::String) {
            auto outerBlock =
                functionNode->getPropertyValue(propertyName->toString());
            if (outerBlock) {
              if (outerBlock->getNodeType() == AST::Entity ||
                  outerBlock->getNodeType() == AST::Array) {
                matchedInst = ASTQuery::findDeclarationByName(
                    ASTQuery::getNodeName(outerBlock), scopeStack, tree);
                break;
              } else if (outerBlock->getNodeType() == AST::Function ||
                         outerBlock->getNodeType() == AST::Expression ||
                         outerBlock->getNodeType() == AST::List) {
                matchedInst = outerBlock;
                break;
              } else if (outerBlock->getNodeType() == AST::Int ||
                         outerBlock->getNodeType() == AST::Real ||
                         outerBlock->getNodeType() == AST::Switch) {
                matchedInst = outerBlock;
                break;
              }
            }
          }
        }
      }
    }
  }
  return matchedInst;
}

std::vector<ASTNode> CodeAnalysis::getEntitiesInScope(ASTNode root,
                                                      ScopeStack scopeStack,
                                                      ASTNode tree) {
  std::vector<ASTNode> blocks;
  if (root->getNodeType() == AST::Declaration ||
      root->getNodeType() == AST::ArrayDeclaration) {
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(root);

    if (decl->getPropertyValue("ports")) {
      for (const ASTNode &block :
           decl->getPropertyValue("ports")->getChildren()) {
        blocks.push_back(block);
      }
    }
  } else if (root->getNodeType() == AST::List) {
    std::vector<ASTNode> elements =
        std::static_pointer_cast<ListNode>(root)->getChildren();
    for (const ASTNode &element : elements) {
      auto newBlocks = getEntitiesInScope(element, scopeStack, tree);
      blocks.insert(blocks.end(), newBlocks.begin(), newBlocks.end());
    }
  } else if (root->getNodeType() == AST::Entity) {
    std::shared_ptr<EntityNode> name =
        std::static_pointer_cast<EntityNode>(root);
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(name->getName(), scopeStack, tree);
    if (declaration) {
      blocks = getEntitiesInScope(declaration, scopeStack, tree);
    }
  } else if (root->getNodeType() == AST::Array) {
    std::shared_ptr<ArrayNode> name = std::static_pointer_cast<ArrayNode>(root);
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(name->getName(), scopeStack, tree);
    if (declaration) {
      blocks = getEntitiesInScope(declaration, scopeStack, tree);
    }
  } else {
    for (const ASTNode &child : root->getChildren()) {
      auto newBlocks = getEntitiesInScope(child, scopeStack, tree);
      blocks.insert(blocks.end(), newBlocks.begin(), newBlocks.end());
    }
  }
  return blocks;
}

int CodeAnalysis::evaluateSizeMemberAccess(
    std::string targetPortName, ScopeStack scopeStack,
    std::shared_ptr<DeclarationNode> decl, std::shared_ptr<FunctionNode> func,
    ASTNode tree) {
  auto portsNode = decl->getPropertyValue("ports");
  auto blocksNode = decl->getPropertyValue("blocks");
  int portSize = 0;
  if (portsNode && blocksNode) {
    std::vector<ASTNode> ports = portsNode->getChildren();
    std::vector<ASTNode> blocks = blocksNode->getChildren();
    for (const auto &port : ports) {
      if (port->getNodeType() == AST::Declaration) {
        auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
        auto portName = portDecl->getName();
        if (portName == targetPortName) {
          auto portBlock = portDecl->getPropertyValue("block");
          auto portNameNode = portDecl->getPropertyValue("name");
          std::string portName;

          if (portNameNode && portNameNode->getNodeType() == AST::String) {
            portName = std::static_pointer_cast<ValueNode>(portNameNode)
                           ->getStringValue();
          }
          std::string portBlockName;
          if (portBlock && (portBlock->getNodeType() == AST::Entity ||
                            portBlock->getNodeType() == AST::Array)) {
            if (portDecl->getEntityType() == "mainInputPort") {
              portSize =
                  CodeAnalysis::getNodeNumInputs(portBlock, scopeStack, tree);
              if (portSize == -2) {
                std::pair<ASTNode, std::vector<ASTNode>> innerScope =
                    scopeStack.back();
                scopeStack.pop_back();
                // We need to look one scope up.
                auto inBlock =
                    func ? func->getCompilerProperty("inputBlock") : nullptr;
                if (inBlock) {
                  portSize = CodeAnalysis::getNodeNumOutputs(
                      inBlock, scopeStack, tree);
                } else {
                  portSize = 1;
                }
                scopeStack.push_back(innerScope);
              }

            } else if (portDecl->getEntityType() == "mainOutputPort") {
              portSize =
                  CodeAnalysis::getNodeNumOutputs(portBlock, scopeStack, tree);
              if (portSize == CodeAnalysis::SIZE_MEMBER_ACCESS) {
                std::pair<ASTNode, std::vector<ASTNode>> innerScope =
                    scopeStack.back();
                scopeStack.pop_back();
                // We need to look one scope up.
                portSize = CodeAnalysis::getNodeNumOutputs(
                    func->getPropertyValue(portName), scopeStack, tree);
                scopeStack.push_back(innerScope);
              }
            } else if (portDecl->getEntityType() == "propertyInputPort") {
              portSize =
                  CodeAnalysis::getNodeNumInputs(portBlock, scopeStack, tree);
              if (portSize == CodeAnalysis::SIZE_MEMBER_ACCESS) {
                std::pair<ASTNode, std::vector<ASTNode>> innerScope =
                    scopeStack.back();
                scopeStack.pop_back();
                // We need to look one scope up.
                portSize = CodeAnalysis::getNodeNumOutputs(
                    func->getPropertyValue(portName), scopeStack, tree);
                scopeStack.push_back(innerScope);
              }

            } else if (portDecl->getEntityType() == "propertyOutputPort") {
              portSize =
                  CodeAnalysis::getNodeNumOutputs(portBlock, scopeStack, tree);

              if (portSize == CodeAnalysis::SIZE_MEMBER_ACCESS) {
                std::pair<ASTNode, std::vector<ASTNode>> innerScope =
                    scopeStack.back();
                scopeStack.pop_back();
                // We need to look one scope up.
                portSize = CodeAnalysis::getNodeNumOutputs(
                    func->getPropertyValue(portName), scopeStack, tree);
                scopeStack.push_back(innerScope);
              }
            }
          } else {
            std::cerr << "Unexpected port block entry" << std::endl;
          }
        }
      }
    }
  }
  return portSize;
}

double CodeAnalysis::evaluateRateMemberAccess(
    std::string targetPortName, ScopeStack scopeStack,
    std::shared_ptr<DeclarationNode> decl, std::shared_ptr<FunctionNode> func,
    ASTNode tree) {
  auto portsNode = decl->getPropertyValue("ports");
  auto blocksNode = decl->getPropertyValue("blocks");

  std::vector<ASTNode> ports = portsNode->getChildren();
  std::vector<ASTNode> blocks = blocksNode->getChildren();
  for (const auto &port : ports) {
    if (port->getNodeType() == AST::Declaration) {
      auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
      auto portName = portDecl->getName();
      if (portName == targetPortName) {
        auto portBlock = portDecl->getPropertyValue("block");
        auto portNameNode = portDecl->getPropertyValue("name");
        std::string portName;

        if (portNameNode && portNameNode->getNodeType() == AST::String) {
          portName = std::static_pointer_cast<ValueNode>(portNameNode)
                         ->getStringValue();
        }
        std::string portBlockName;
        if (portBlock && (portBlock->getNodeType() == AST::Entity ||
                          portBlock->getNodeType() == AST::Array)) {
          if (portDecl->getEntityType() == "mainInputPort") {
            double rate =
                CodeAnalysis::resolveRate(portBlock, scopeStack, tree, false);
            if (rate == -1) {
              auto inBlock =
                  func ? func->getCompilerProperty("inputBlock") : nullptr;
              if (!inBlock) {
                return 1.0;
              }
              auto resolvedBlock =
                  CodeAnalysis::getInstance(inBlock, scopeStack, tree);
              auto scopeStackOuter = scopeStack;
              for (const auto &subScope : scopeStack) {
                rate = CodeAnalysis::resolveRate(resolvedBlock, scopeStack,
                                                 tree, true);
                if (rate != -1) {
                  return rate;
                }
                if (subScope.first->getNodeType() != AST::Function) {
                  // If no longer a module instance (i.e. AST::Function), the
                  // rate cannot be resolved
                  break;
                }
                auto functionNode =
                    std::static_pointer_cast<FunctionNode>(subScope.first);
                // TODO check framework too
                std::string framework;
                auto funcDecl = ASTQuery::findDeclarationByName(
                    functionNode->getName(), scopeStackOuter, tree,
                    functionNode->getNamespaceList(), framework);
                if (resolvedBlock->getNodeType() == AST::Declaration ||
                    resolvedBlock->getNodeType() == AST::ArrayDeclaration) {
                  resolvedBlock = getMatchedOuterInstance(
                      functionNode,
                      std::static_pointer_cast<DeclarationNode>(resolvedBlock),
                      funcDecl, scopeStackOuter, tree);
                  scopeStackOuter.pop_back();
                }
              }
            }
            return rate;

          } else if (portDecl->getEntityType() == "mainOutputPort") {
            double rate =
                CodeAnalysis::resolveRate(portBlock, scopeStack, tree, false);
            if (rate == -1) {
              auto resolvedBlock = CodeAnalysis::getInstance(
                  func->getCompilerProperty("outputBlock"), scopeStack, tree);
              auto scopeStackOuter = scopeStack;
              for (const auto &subScope : scopeStack) {
                rate = CodeAnalysis::resolveRate(resolvedBlock, scopeStack,
                                                 tree, true);
                if (rate != -1) {
                  return rate;
                }
                if (!subScope.first ||
                    subScope.first->getNodeType() != AST::Function) {
                  // If no longer a module instance (i.e. AST::Function), the
                  // rate cannot be resolved
                  break;
                }
                auto functionNode =
                    std::static_pointer_cast<FunctionNode>(subScope.first);
                // TODO check framework too
                std::string framework;
                auto funcDecl = ASTQuery::findDeclarationByName(
                    functionNode->getName(), scopeStackOuter, tree,
                    functionNode->getNamespaceList(), framework);
                if (resolvedBlock->getNodeType() == AST::Declaration ||
                    resolvedBlock->getNodeType() == AST::ArrayDeclaration) {
                  resolvedBlock = getMatchedOuterInstance(
                      functionNode,
                      std::static_pointer_cast<DeclarationNode>(resolvedBlock),
                      funcDecl, scopeStackOuter, tree);
                  scopeStackOuter.pop_back();
                }
              }
            }
            return rate;

          } else if (portDecl->getEntityType() == "propertyInputPort" ||
                     portDecl->getEntityType() == "propertyOutputPort") {
            double rate =
                CodeAnalysis::resolveRate(portBlock, scopeStack, tree, false);
            if (rate == -1) {
              auto resolvedBlock =
                  CodeAnalysis::getInstance(portBlock, scopeStack, tree);
              auto scopeStackOuter = scopeStack;
              for (const auto &subScope : scopeStack) {
                rate = CodeAnalysis::resolveRate(resolvedBlock, scopeStack,
                                                 tree, true);
                if (rate != -1) {
                  return rate;
                }
                if (subScope.first->getNodeType() != AST::Function) {
                  // If no longer a module instance (i.e. AST::Function), the
                  // rate cannot be resolved
                  break;
                }
                auto functionNode =
                    std::static_pointer_cast<FunctionNode>(subScope.first);
                // TODO check framework too
                std::string framework;
                auto funcDecl = ASTQuery::findDeclarationByName(
                    functionNode->getName(), scopeStackOuter, tree,
                    functionNode->getNamespaceList(), framework);
                if (resolvedBlock->getNodeType() == AST::Declaration ||
                    resolvedBlock->getNodeType() == AST::ArrayDeclaration) {
                  resolvedBlock = getMatchedOuterInstance(
                      functionNode,
                      std::static_pointer_cast<DeclarationNode>(resolvedBlock),
                      funcDecl, scopeStackOuter, tree);
                  scopeStackOuter.pop_back();
                }
              }
            }
            return rate;
          }
        } else {
          std::cerr << "Unexpected port block entry" << std::endl;
        }
      }
    }
  }
  return -1.0;
}

std::vector<std::shared_ptr<MemberAccessNode>>
CodeAnalysis::getUsedMemberAccesses(std::shared_ptr<DeclarationNode> funcDecl) {
  std::vector<std::shared_ptr<MemberAccessNode>> used;

  auto blocksNode = funcDecl->getPropertyValue("blocks");
  if (blocksNode) {
    for (const auto &node : blocksNode->getChildren()) {
      if (node->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(node);
        auto newUsed = CodeAnalysis::getUsedMemberAccesses(decl);
        used.insert(used.end(), newUsed.begin(), newUsed.end());
      }
    }
  }

  auto streamBlocks = ASTQuery::getContainedStreamBlocks(funcDecl, {}, nullptr);
  for (const auto &block : streamBlocks) {
    for (const auto &stream : block.streams) {
      StreamNodeIterator it(stream);
      while (it.hasNext()) {
        ASTNode node = it.next();
        auto newUsed = getUsedMemberAccessesInNode(node);
        used.insert(used.end(), newUsed.begin(), newUsed.end());
      }
    }
  }

  return used;
}

std::vector<std::shared_ptr<MemberAccessNode>>
CodeAnalysis::getUsedMemberAccessesInNode(ASTNode node) {
  std::vector<std::shared_ptr<MemberAccessNode>> used;
  if (node->getNodeType() == AST::MemberAccess) {
    used.push_back(std::static_pointer_cast<MemberAccessNode>(node));
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    for (const auto &elem : node->getChildren()) {
      auto newUsed = getUsedMemberAccessesInNode(elem);
      used.insert(used.end(), newUsed.begin(), newUsed.end());
    }
  } else if (node->getNodeType() == AST::Array) {
    auto index = std::static_pointer_cast<ArrayNode>(node)->index();
    if (index) {
      for (const auto &indexNode : index->getChildren()) {
        auto newUsed = getUsedMemberAccessesInNode(indexNode);
        used.insert(used.end(), newUsed.begin(), newUsed.end());
      }
    }
  }
  return used;
}

std::vector<ASTNode> CodeAnalysis::getUsedEntitiesInNode(ASTNode node) {
  std::vector<ASTNode> used;
  if (!node)
    return used;

  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    used.push_back(node);
    if (node->getNodeType() == AST::Array) {
      auto index = std::static_pointer_cast<ArrayNode>(node)->index();
      if (index) {
        for (const auto &indexNode : index->getChildren()) {
          auto newUsed = getUsedEntitiesInNode(indexNode);
          used.insert(used.end(), newUsed.begin(), newUsed.end());
        }
      }
    }
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    for (const auto &elem : node->getChildren()) {
      auto newUsed = getUsedEntitiesInNode(elem);
      used.insert(used.end(), newUsed.begin(), newUsed.end());
    }
  }
  return used;
}

std::vector<ASTNode> CodeAnalysis::getUsedEntitiesInStreams(
    std::shared_ptr<DeclarationNode> funcDecl) {
  std::vector<ASTNode> used;
  auto streamBlocks = ASTQuery::getContainedStreamBlocks(funcDecl, {}, nullptr);
  for (const auto &block : streamBlocks) {
    for (const auto &stream : block.streams) {
      StreamNodeIterator it(stream);
      while (it.hasNext()) {
        ASTNode node = it.next();
        auto newUsed = getUsedEntitiesInNode(node);
        used.insert(used.end(), newUsed.begin(), newUsed.end());
      }
    }
  }
  return used;
}

ASTNode CodeAnalysis::resolveDomain(ASTNode node, ScopeStack scopeStack,
                                    ASTNode tree, bool downStream) {
  auto blockDecl =
      CodeAnalysis::resolveConnectionEntity(node, scopeStack, tree, downStream);
  if (blockDecl) {
    return CodeAnalysis::getNodeDomain(blockDecl, scopeStack, tree);
  } else {
    return CodeAnalysis::getNodeDomain(node, scopeStack, tree);
  }
}

double CodeAnalysis::resolveRate(ASTNode node, ScopeStack scopeStack,
                                 ASTNode tree, bool downStream) {
  auto blockDecl =
      CodeAnalysis::resolveConnectionEntity(node, scopeStack, tree, downStream);
  if (blockDecl) {
    return CodeAnalysis::getNodeRate(blockDecl, scopeStack, tree);
  } else if (node) {
    return CodeAnalysis::getNodeRate(node, scopeStack, tree);
  } else {
    return -1;
  }
}

int CodeAnalysis::getNodeNumOutputs(ASTNode node, const ScopeStack &scope,
                                    ASTNode tree,
                                    std::vector<LangError> *errors) {
  if (!node) {
    return 1;
  }
  if (node->getNodeType() == AST::List) {
    int size = 0;
    for (const ASTNode &member : node->getChildren()) {
      auto newSize =
          CodeAnalysis::getNodeNumOutputs(member, scope, tree, errors);
      if (newSize >= 0) {
        size += newSize;
      } else {
        assert(0 == 1); // FIXME implement
      }
    }
    return size;
  } else if (node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::Real ||
             node->getNodeType() == AST::String ||
             node->getNodeType() == AST::Switch) {
    return 1;
  } else if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      return getNodeNumOutputs(expr->getValue(), scope, tree, errors);
    } else {
      auto leftSize = getNodeNumOutputs(expr->getLeft(), scope, tree, errors);
      auto rightSize = getNodeNumOutputs(expr->getRight(), scope, tree, errors);
      auto size = std::max(leftSize, rightSize);
      return size;
    }
  } else if (node->getNodeType() == AST::Function) {
    std::shared_ptr<FunctionNode> func =
        std::static_pointer_cast<FunctionNode>(node);
    std::shared_ptr<DeclarationNode> platformFunc =
        ASTQuery::findDeclarationByName(func->getName(), scope, tree);
    int dataSize = CodeAnalysis::getFunctionDataSize(func, scope, tree, errors);
    if (platformFunc) {
      return getTypeNumOutputs(platformFunc, scope, tree, errors) * dataSize;
    } else {
      return CodeAnalysis::SIZE_UNKNOWN;
    }
  } else if (node->getNodeType() == AST::Array) {
    return ASTQuery::getArraySize(std::static_pointer_cast<ArrayNode>(node),
                                  scope, tree, errors);
  } else if (node->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(node.get());
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(
            node->getCompilerProperty("declaration"));
    if (!decl) {
      decl = ASTQuery::findDeclarationByName(name->getName(), scope, tree);
    }
    if (decl) {
      return getTypeNumOutputs(decl, scope, tree, errors);
    } else {
      return CodeAnalysis::SIZE_UNKNOWN;
    }
  } else if (node->getNodeType() == AST::MemberAccess) {
    std::shared_ptr<MemberAccessNode> portProp =
        std::static_pointer_cast<MemberAccessNode>(node);
    if (portProp->getPropertyName() == "size" ||
        portProp->getPropertyName() == "rate" ||
        portProp->getPropertyName() == "domain") {
      return CodeAnalysis::SIZE_MEMBER_ACCESS;
    } else {
      std::cerr
          << "Unknown member access in getNodeNumOutputs() setting size to 1"
          << std::endl;
      return 1;
    }
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    return ASTQuery::getEntityDeclaredSize(
        std::static_pointer_cast<DeclarationNode>(node), scope, tree, errors);
  } else if (node->getNodeType() == AST::GeneratorRange) {
    auto genRange = std::static_pointer_cast<GeneratorRangeNode>(node);
    std::vector<LangError> evalErrors;
    int64_t start = ASTFunctions::evaluateConstInteger(
        genRange->startIndex(), scope, tree, &evalErrors);
    int64_t end = ASTFunctions::evaluateConstInteger(
        genRange->endIndex(), scope, tree, &evalErrors);
    int64_t step = 1;
    if (genRange->stepIndex()) {
      step = ASTFunctions::evaluateConstInteger(
          genRange->stepIndex(), scope, tree, &evalErrors);
    }
    if (evalErrors.empty() && step != 0) {
      if (step > 0 && end >= start) {
        return (int)((end - start) / step + 1);
      } else if (step < 0 && end <= start) {
        return (int)((start - end) / (-step) + 1);
      } else {
        return 0;
      }
    }
    return CodeAnalysis::SIZE_UNKNOWN;
  }
  return CodeAnalysis::SIZE_UNKNOWN;
}

int CodeAnalysis::getNodeNumInputs(ASTNode node, ScopeStack scope, ASTNode tree,
                                   std::vector<LangError> *errors) {
  if (node->getNodeType() == AST::Function) {
    std::shared_ptr<FunctionNode> func =
        std::static_pointer_cast<FunctionNode>(node);
    std::shared_ptr<DeclarationNode> platformFunc =
        std::static_pointer_cast<DeclarationNode>(
            node->getCompilerProperty("declaration"));
    if (!platformFunc) {
      platformFunc =
          ASTQuery::findDeclarationByName(func->getName(), scope, tree);
    }
    int dataSize = CodeAnalysis::getFunctionDataSize(func, scope, tree, errors);
    if (platformFunc) {
      if (platformFunc->getEntityType() == "reaction") {
        return 1; // Reactions always have one input as main port for trigger
      } else {
        auto subScope = ASTQuery::getModuleEntities(platformFunc);
        scope.push_back({func, subScope});

        int numInputs = getTypeNumInputs(platformFunc, scope, tree, errors);
        if (numInputs < 0) {
          return numInputs;
        } else {
          return numInputs * dataSize;
        }
      }
    } else {
      return -1;
    }
  } else if (node->getNodeType() == AST::Stream) {
    StreamNode *stream = static_cast<StreamNode *>(node.get());
    ASTNode left = stream->getLeft();
    int leftSize = CodeAnalysis::getNodeNumInputs(left, scope, tree, errors);
    return leftSize;
  } else if (node->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(node.get());
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(
            node->getCompilerProperty("declaration"));
    if (!decl) {
      decl = ASTQuery::findDeclarationByName(name->getName(), scope, tree);
    }
    if (decl) {
      return getTypeNumInputs(decl, scope, tree, errors);
    } else {
      return CodeAnalysis::SIZE_UNKNOWN;
    }
  } else if (node->getNodeType() == AST::Array) {
    return ASTQuery::getArraySize(std::static_pointer_cast<ArrayNode>(node),
                                  scope, tree, errors);
  } else if (node->getNodeType() == AST::List) {
    int size = 0;
    for (const ASTNode &member : node->getChildren()) {
      size += CodeAnalysis::getNodeNumInputs(member, scope, tree, errors);
    }
    return size;
  } else if (node->getNodeType() == AST::MemberAccess) {
    auto pp = std::static_pointer_cast<MemberAccessNode>(node);
    if (pp->getEntity() == "") {
      // TODO If it's a local port, we should be able to determine size
    }
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    return getTypeNumInputs(std::static_pointer_cast<DeclarationNode>(node),
                            scope, tree, errors);
  } else {
    return 0;
  }
  return CodeAnalysis::SIZE_UNKNOWN;
}

int CodeAnalysis::getTypeNumOutputs(
    std::shared_ptr<DeclarationNode> blockDeclaration, const ScopeStack &scope,
    ASTNode tree, std::vector<LangError> *errors) {
  if (blockDeclaration->getNodeType() == AST::ArrayDeclaration) {
    return ASTQuery::getEntityDeclaredSize(blockDeclaration, scope, tree,
                                           errors);
  } else if (blockDeclaration->getNodeType() == AST::Declaration) {
    if (blockDeclaration->getEntityType() == "module") {
      std::shared_ptr<DeclarationNode> portBlock =
          ASTQuery::getModuleMainOutputPort(blockDeclaration);

      EntityNode *outputName = nullptr;
      if (portBlock) {
        if (portBlock->getPropertyValue("block")->getNodeType() ==
            AST::Entity) {
          outputName = static_cast<EntityNode *>(
              portBlock->getPropertyValue("block").get());
        } else {
          std::cerr << "WARNING: Expecting name node for output entity"
                    << std::endl;
        }
      }
      if (!outputName || outputName->getNodeType() == AST::None) {
        return 0;
      }
      auto subScope = ASTQuery::getModuleEntities(
          std::static_pointer_cast<DeclarationNode>(blockDeclaration));
      if (subScope.size() > 0) {
        assert(outputName->getNodeType() == AST::Entity);
        std::string outputBlockName = outputName->getName();
        for (const ASTNode &internalDeclarationNode : subScope) {
          if (internalDeclarationNode->getNodeType() == AST::ArrayDeclaration ||
              internalDeclarationNode->getNodeType() == AST::Declaration) {
            std::string blockName =
                static_cast<DeclarationNode *>(internalDeclarationNode.get())
                    ->getName();
            if (blockName == outputBlockName) {
              std::shared_ptr<DeclarationNode> intBlock =
                  std::static_pointer_cast<DeclarationNode>(
                      internalDeclarationNode);
              if (intBlock->getName() == outputBlockName) {
                if (internalDeclarationNode->getNodeType() ==
                    AST::ArrayDeclaration) {
                  return ASTQuery::getEntityDeclaredSize(intBlock, scope, tree,
                                                         errors);
                } else if (internalDeclarationNode->getNodeType() ==
                           AST::Declaration) {
                  return 1;
                }
              }
            }
          }
        }
      }
    } else if (blockDeclaration->getEntityType() == "platformModule") {
      auto outputList = blockDeclaration->getPropertyValue("outputs");
      if (outputList && outputList->getNodeType() == AST::List) {
        return outputList->getChildren().size();
      }
    } else if (blockDeclaration->getEntityType() == "buffer") {
      auto sizeNode = blockDeclaration->getPropertyValue("size");
      if (sizeNode->getNodeType() == AST::Int) {
        return std::static_pointer_cast<ValueNode>(sizeNode)->getIntValue();
      }
    }
    return 1;
  }
  return 0;
}

int CodeAnalysis::getTypeNumInputs(
    std::shared_ptr<DeclarationNode> blockDeclaration, const ScopeStack &scope,
    ASTNode tree, std::vector<LangError> *errors) {
  if (blockDeclaration->getNodeType() == AST::ArrayDeclaration) {
    return ASTQuery::getEntityDeclaredSize(blockDeclaration, scope, tree,
                                           errors);
  } else if (blockDeclaration->getNodeType() == AST::Declaration) {
    if (blockDeclaration->getEntityType() == "module" ||
        blockDeclaration->getEntityType() == "loop") {
      auto subScope = ASTQuery::getModuleEntities(
          std::static_pointer_cast<DeclarationNode>(blockDeclaration));
      if (subScope.size() > 0) {
        EntityNode *inputName = nullptr;

        std::shared_ptr<DeclarationNode> portBlock =
            ASTQuery::getModuleMainInputPort(blockDeclaration);
        if (portBlock) {
          if (portBlock->getPropertyValue("block")->getNodeType() ==
              AST::Entity) {
            inputName = static_cast<EntityNode *>(
                portBlock->getPropertyValue("block").get());
          } else {
            std::cerr << "WARNING: Expecting name node for input entity"
                      << std::endl;
          }
        }
        if (!inputName || inputName->getNodeType() == AST::None) {
          return 0;
        }
        assert(inputName->getNodeType() == AST::Entity);
        std::string inputBlockName = inputName->getName();
        for (const ASTNode &internalDeclarationNode : subScope) {
          if (!internalDeclarationNode) {
            return CodeAnalysis::SIZE_UNKNOWN;
          }
          if (internalDeclarationNode->getNodeType() == AST::Declaration ||
              internalDeclarationNode->getNodeType() == AST::ArrayDeclaration) {
            std::string blockName =
                static_cast<DeclarationNode *>(internalDeclarationNode.get())
                    ->getName();
            if (blockName == inputBlockName) {
              if (internalDeclarationNode->getNodeType() ==
                  AST::ArrayDeclaration) {
                std::shared_ptr<DeclarationNode> intBlock =
                    std::static_pointer_cast<DeclarationNode>(
                        internalDeclarationNode);
                return ASTQuery::getEntityDeclaredSize(intBlock, scope, tree,
                                                       errors);
              } else if (internalDeclarationNode->getNodeType() ==
                         AST::Declaration) {
                return 1;
              }
            }
          }
        }
      }
    } else if (blockDeclaration->getEntityType() == "platformModule") {
      auto outputList = blockDeclaration->getPropertyValue("inputs");
      if (outputList && outputList->getNodeType() == AST::List) {
        for (const auto &child : outputList->getChildren()) {
          if (child && child->getNodeType() == AST::Declaration) {
            auto entType =
                std::static_pointer_cast<DeclarationNode>(child)->getEntityType();
            if (entType == "variadic" || entType == "constrainedList") {
              return CodeAnalysis::SIZE_MEMBER_ACCESS;
            }
          }
        }
        return outputList->getChildren().size();
      }
    } else {
      int size = ASTQuery::getNodeSize(blockDeclaration, scope, tree);
      int internalSize = 1;
      auto typeDeclaration = ASTQuery::findTypeDeclarationByName(
          blockDeclaration->getEntityType(), scope, tree);
      if (typeDeclaration &&
          (typeDeclaration->getEntityType() == "platformModule" ||
           typeDeclaration->getEntityType() == "platformBlock")) {
        auto inputs = typeDeclaration->getPropertyValue("inputs");
        if (inputs && inputs->getNodeType() == AST::List) {
          for (const auto &child : inputs->getChildren()) {
            if (child && child->getNodeType() == AST::Declaration) {
              auto entType = std::static_pointer_cast<DeclarationNode>(child)
                                 ->getEntityType();
              if (entType == "variadic" || entType == "constrainedList") {
                return CodeAnalysis::SIZE_MEMBER_ACCESS;
              }
            }
          }
          internalSize = inputs->getChildren().size();
        }
      }
      return size * internalSize;
    }
    return 1;
  }
  return 0;
}

int CodeAnalysis::getFunctionDataSize(std::shared_ptr<FunctionNode> func,
                                      ScopeStack scope, ASTNode tree,
                                      std::vector<LangError> *errors) {
  auto ports = func->getProperties();
  if (ports.size() == 0) {
    return 1;
  }
  int size = 1;
  for (const std::shared_ptr<PropertyNode> &port : ports) {
    ASTNode value = port->getValue();
    // FIXME need to decide the size by also looking at the port entity size
    int newSize = CodeAnalysis::getNodeNumOutputs(value, scope, tree, errors);
    auto decl = ASTQuery::findDeclarationByName(func->getName(), scope, tree,
                                                func->getNamespaceList());
    if (decl && decl->getPropertyValue("ports")) {
      std::shared_ptr<DeclarationNode> declaredPort;
      for (const auto &portDecl :
           decl->getPropertyValue("ports")->getChildren()) {
        if (portDecl->getNodeType() == AST::Declaration) {
          auto nameNode = std::static_pointer_cast<DeclarationNode>(portDecl)
                              ->getPropertyValue("name");
          if (nameNode && nameNode->getNodeType() == AST::String) {
            if (port->getName() == std::static_pointer_cast<ValueNode>(nameNode)
                                       ->getStringValue()) {
              declaredPort =
                  std::static_pointer_cast<DeclarationNode>(portDecl);
              break;
            }
          }
        }
      }
      if (declaredPort) {
        auto portEntityNode = declaredPort->getPropertyValue("block");
        auto blocks = decl->getPropertyValue("blocks");
        if (portEntityNode && blocks) {
          std::string portEntityName;
          if (portEntityNode->getNodeType() == AST::Entity) {
            portEntityName =
                std::static_pointer_cast<EntityNode>(portEntityNode)->getName();
          } else if (portEntityNode->getNodeType() == AST::Array) {
            portEntityName =
                std::static_pointer_cast<ArrayNode>(portEntityNode)->getName();
          }
          auto portBlockDecl = ASTQuery::findDeclarationByName(
              portEntityName, {{nullptr, blocks->getChildren()}}, nullptr);
          if (declaredPort->getEntityType() == "mainInputPort" ||
              declaredPort->getEntityType() == "propertyInputPort") {
            size = CodeAnalysis::getNodeNumInputs(
                portEntityNode, {{nullptr, blocks->getChildren()}}, tree,
                errors);
          } else if (declaredPort->getEntityType() == "mainOutputPort" ||
                     declaredPort->getEntityType() == "propertyOutputPort") {
            size = CodeAnalysis::getNodeNumOutputs(
                portEntityNode, {{nullptr, blocks->getChildren()}}, tree,
                errors);
          }
        }
      }
    }
    if (size != newSize) {
      if (size == 1 && newSize > 0) {
        size = newSize;
      } else if (newSize == 1) {
        // just ignore
      } else {
        //        size = -1; // TODO should this be a reported error?
      }
    }
  }
  return size;
}

int CodeAnalysis::getFunctionNumInstances(std::shared_ptr<FunctionNode> func,
                                          ScopeStack scope, ASTNode tree,
                                          std::vector<LangError> *errors) {
  auto portProperties = func->getProperties();
  int numInstances = 1;
  for (const auto &propertyNode : portProperties) {
    auto portBlock = propertyNode->getValue();
    auto portName = propertyNode->getName();
    auto funcDecl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(func),
                                                    scope, tree);
    if (funcDecl) {
      if (funcDecl->getPropertyValue("blocks")) {
        scope.push_back(
            {func, funcDecl->getPropertyValue("blocks")->getChildren()});
      }
      auto ports = funcDecl->getPropertyValue("ports");
      if (ports) {
        // Match port in declaration to current property
        // This will tell us how many instances we should make
        for (const auto &port : ports->getChildren()) {
          if (port->getNodeType() == AST::Declaration) {
            auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
            auto nameNode = portDecl->getPropertyValue("name");
            if (!(nameNode->getNodeType() == AST::String)) {
              continue;
            }
            if (std::static_pointer_cast<ValueNode>(nameNode)
                    ->getStringValue() == portName) {
              int propertyBlockSize = 0;
              if (portDecl->getEntityType() == "mainInputPort" ||
                  portDecl->getEntityType() == "propertyInputPort") {
                propertyBlockSize = CodeAnalysis::getNodeNumOutputs(
                    portBlock, scope, tree, errors);
              } else {
                propertyBlockSize = CodeAnalysis::getNodeNumInputs(
                    portBlock, scope, tree, errors);
              }
              auto internalPortBlock = portDecl->getPropertyValue("block");
              if (internalPortBlock) {
                int internalBlockSize;

                if (portDecl->getEntityType() == "mainInputPort" ||
                    portDecl->getEntityType() == "propertyInputPort") {
                  internalBlockSize = CodeAnalysis::getNodeNumOutputs(
                      internalPortBlock, scope, tree, errors);
                } else {
                  internalBlockSize = CodeAnalysis::getNodeNumInputs(
                      internalPortBlock, scope, tree, errors);
                }
                //                numInstances = 1;
                if (internalBlockSize == -2) {
                  // FIXME this assumes that the block is given the size of its
                  // own port, but it is possible it could be give the size of
                  // another port, causing an error or a different number of
                  // instances
                  return 1;
                }
                int portNumInstances = propertyBlockSize / internalBlockSize;
                assert(propertyBlockSize / internalBlockSize ==
                       float(propertyBlockSize) / internalBlockSize);
                if (numInstances == 0) {
                  numInstances = portNumInstances;
                } else if (numInstances == 1 && portNumInstances > 1) {
                  numInstances = portNumInstances;
                } else if (numInstances != portNumInstances &&
                           portNumInstances != 1) {
                  std::cerr << "ERROR size mismatch in function ports"
                            << std::endl;
                }
              }
            }
          }
        }
      }
    }
  }
  return numInstances;
}

std::shared_ptr<DeclarationNode>
CodeAnalysis::findDataTypeDeclaration(std::string dataTypeName, ASTNode tree) {
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      auto nodeDecl = std::static_pointer_cast<DeclarationNode>(node);
      if (nodeDecl->getEntityType() == "platformDataType") {
        auto typeProp = nodeDecl->getPropertyValue("type");
        if (typeProp && typeProp->getNodeType() == AST::String) {
          if (std::static_pointer_cast<ValueNode>(typeProp)->getStringValue() ==
              dataTypeName) {
            return nodeDecl;
          }
        }
      }
    }
  }
  return nullptr;
}

std::vector<ASTNode> CodeAnalysis::getInputDataTypes(ASTNode node,
                                                     const ScopeStack &scope,
                                                     ASTNode tree) {
  // TODO identify signal and module from inheritance
  // TODO support array
  if (node->getNodeType() == AST::Entity) {
    auto entityNode = std::static_pointer_cast<EntityNode>(node);
    auto decl = ASTQuery::findDeclarationByName(entityNode->getName(), scope, tree);
    if (!decl && tree) {
      decl = ASTQuery::findDeclarationByName(entityNode->getName(), {}, tree);
    }
    if (decl) {
      if (decl->getEntityType() == "signal") {
        // For now only resolve types for signals, perhaps in the future resolve
        // from all declarations that inherit from "Typed"
        return {getDataTypeForSignalDeclaration(decl)};
      }
      if (decl->getEntityType() == "switch" ||
          decl->getEntityType() == "trigger" ||
          decl->getEntityType() == "string") {
        // Builtin data type
        return {std::make_shared<EntityNode>(
            getDataTypeForDeclaration(decl, scope, tree), __FILE__, __LINE__)};
      } else {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Could not find type property in:" << decl->toText()
                  << std::endl;
      }
    } else {
      std::cerr << __FILE__ << ":" << __LINE__
                << " Could not find node declaration for " << entityNode->getName()
                << std::endl;
    }
  } else if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    auto decl =
        ASTQuery::findDeclarationByName(arrayNode->getName(), scope, tree);
    if (!decl && tree) {
      decl = ASTQuery::findDeclarationByName(arrayNode->getName(), {}, tree);
    }
    if (decl) {
      if (decl->getEntityType() == "signal") {
        // For now only resolve types for signals, perhaps in the future
        // resolve from all declarations that inherit from "Typed"
        return {getDataTypeForSignalDeclaration(decl)};
      }
      if (decl->getEntityType() == "switch" ||
          decl->getEntityType() == "trigger" ||
          decl->getEntityType() == "string") {
        // Builtin data type
        return {std::make_shared<EntityNode>(
            getDataTypeForDeclaration(decl, scope, tree), __FILE__, __LINE__)};
      }
    } else {
      std::cerr << __FILE__ << ":" << __LINE__
                << " Could not find node declaration for "
                << arrayNode->getName() << std::endl;
    }
  } else if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    auto funcDecl =
        ASTQuery::findDeclarationByName(func->getName(), scope, tree);
    if (!funcDecl) {
      return {};
    }
    // first check if input entity in module has a defined type
    auto inputPortBlock = ASTQuery::getModuleMainInputPort(funcDecl);
    if (inputPortBlock) {
      auto inputEntityNode = inputPortBlock->getPropertyValue("block");
      if (inputEntityNode) {
        auto blockNodeDecl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(inputEntityNode), scope,
            funcDecl->getPropertyValue("blocks"));
        if (blockNodeDecl &&
            blockNodeDecl->getEntityType() ==
                "signal") { // TODO do this for all "typed" entities
          auto dataType = getDataTypeForSignalDeclaration(blockNodeDecl);
          if (dataType) {
            if (dataType->getNodeType() == AST::MemberAccess) {
              auto dataTypePortBlock =
                  std::static_pointer_cast<MemberAccessNode>(dataType);
              if (dataTypePortBlock->getEntity() == inputPortBlock->getName()) {
                auto connected = func->getCompilerProperty("mainInput");
                if (connected) {
                  auto connectedDecl =
                      connected->getCompilerProperty("declaration");
                  if (connectedDecl &&
                      connectedDecl->getNodeType() == AST::Declaration) {
                    return {
                        std::static_pointer_cast<DeclarationNode>(connectedDecl)
                            ->getPropertyValue("type")};
                  }
                }
              } else {
                // TODO resolve for function secondary ports
                // https://github.com/StrideLang/stridegen/issues/3
                auto outputPortBlock =
                    ASTQuery::getModuleMainOutputPort(funcDecl);
                if (outputPortBlock) {
                  if (dataTypePortBlock->getEntity() ==
                      outputPortBlock->getName()) {
                    auto connected = func->getCompilerProperty("mainOutput");
                    if (connected) {
                      return getInputDataTypes(connected, scope, tree);
                    } else {
                      return {};
                    }
                  }
                }
              }
            } else {
              return {dataType};
            }
          }
        }
      }
    }

    // then check to see if the outer input entity has a defined type
    if (auto outerInputBlock = func->getCompilerProperty("inputBlock")) {
      // TODO complete
    }
    // finally resolve the type from streams inside the function declaration
    // and the connection to the outer output entity

    // TODO implement
    return {};
  }
  return {};
}

std::vector<ASTNode> CodeAnalysis::getOutputDataTypes(ASTNode node,
                                                      const ScopeStack &scope,
                                                      ASTNode tree) {
  // TODO identify signal and module from inheritance
  // TODO support array
  if (node->getNodeType() == AST::Entity) {
    auto entityNode = std::static_pointer_cast<EntityNode>(node);
    auto decl = ASTQuery::findDeclarationByName(entityNode->getName(), scope, tree);
    if (!decl && tree) {
      decl = ASTQuery::findDeclarationByName(entityNode->getName(), {}, tree);
    }
    if (decl) {
      if (decl->getEntityType() == "signal") {
        auto type = getDataTypeForSignalDeclaration(decl);
        return {type};
      } else if (decl->getEntityType() == "switch" ||
                 decl->getEntityType() == "trigger" ||
                 decl->getEntityType() == "string" ||
                 ASTQuery::isConstant(decl, scope, tree)) {
        // Builtin data type, or a constant's type taken from its value
        return {std::make_shared<EntityNode>(
            getDataTypeForDeclaration(decl, scope, tree), __FILE__, __LINE__)};
      }
    } else {
      std::cerr << __FILE__ << ":" << __LINE__
                << " Could not find node declaration for " << entityNode->getName()
                << std::endl;
    }
  } else if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    auto decl =
        ASTQuery::findDeclarationByName(arrayNode->getName(), scope, tree);
    if (!decl && tree) {
      decl = ASTQuery::findDeclarationByName(arrayNode->getName(), {}, tree);
    }
    if (decl) {
      if (decl->getEntityType() == "signal") {
        auto type = getDataTypeForSignalDeclaration(decl);
        return {type};
      } else if (decl->getEntityType() == "switch" ||
                 decl->getEntityType() == "trigger" ||
                 decl->getEntityType() == "string" ||
                 ASTQuery::isConstant(decl, scope, tree)) {
        // Builtin data type, or a constant's type taken from its value
        return {std::make_shared<EntityNode>(
            getDataTypeForDeclaration(decl, scope, tree), __FILE__, __LINE__)};
      }
    } else {
      std::cerr << __FILE__ << ":" << __LINE__
                << " Could not find node declaration for "
                << arrayNode->getName() << std::endl;
    }
  } else if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    auto funcDecl =
        ASTQuery::findDeclarationByName(func->getName(), scope, tree);
    if (!funcDecl) {
      return {};
    }
    if (funcDecl->getEntityType() == "platformModule") {
      // Platform modules declare their output types
      std::vector<ASTNode> types;
      auto outputsNode = funcDecl->getPropertyValue("outputs");
      if (outputsNode && outputsNode->getNodeType() == AST::List) {
        for (const auto &output : outputsNode->getChildren()) {
          if (output->getNodeType() != AST::Entity) {
            return {};
          }
          types.push_back(std::make_shared<EntityNode>(
              ASTQuery::getNodeName(output), __FILE__, __LINE__));
        }
      }
      return types;
    }
    // first check if output entity in module has a defined type
    // then check connected entities against port entity definitions
    auto outputPortBlock = ASTQuery::getModuleMainOutputPort(funcDecl);
    if (outputPortBlock) {
      auto outputEntityNode = outputPortBlock->getPropertyValue("block");
      if (outputEntityNode) {
        auto blockNodeDecl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(outputEntityNode), scope,
            funcDecl->getPropertyValue("blocks"));
        if (blockNodeDecl && (blockNodeDecl->getEntityType() == "switch" ||
                              blockNodeDecl->getEntityType() == "trigger" ||
                              blockNodeDecl->getEntityType() == "string")) {
          // Builtin data type
          return {std::make_shared<EntityNode>(
              getDataTypeForDeclaration(blockNodeDecl, scope, tree), __FILE__,
              __LINE__)};
        }
        if (blockNodeDecl && blockNodeDecl->getEntityType() == "signal") {
          auto dataType = getDataTypeForSignalDeclaration(blockNodeDecl);
          if (dataType) {
            if (dataType->getNodeType() == AST::MemberAccess) {
              auto dataTypePortBlock =
                  std::static_pointer_cast<MemberAccessNode>(dataType);
              if (dataTypePortBlock->getEntity() ==
                  outputPortBlock->getName()) {
                auto connected = func->getCompilerProperty("mainOutput");
                if (connected) {
                  auto connectedDecl =
                      connected->getCompilerProperty("declaration");
                  if (connectedDecl &&
                      connectedDecl->getNodeType() == AST::Declaration) {
                    return {
                        std::static_pointer_cast<DeclarationNode>(connectedDecl)
                            ->getPropertyValue("type")};
                  }
                }
              } else {
                // TODO resolve for function secondary ports
                // https://github.com/StrideLang/stridegen/issues/3
                auto inputPortBlock =
                    ASTQuery::getModuleMainInputPort(funcDecl);
                if (inputPortBlock) {
                  if (dataTypePortBlock->getEntity() ==
                      inputPortBlock->getName()) {
                    auto connected = func->getCompilerProperty("mainInput");
                    if (connected) {
                      return getOutputDataTypes(connected, scope, tree);
                    } else {
                      return {};
                    }
                  }
                }
              }
            } else {
              return {dataType};
            }
          }
        }
      }
    }
  } else if (node->getNodeType() == AST::List) {
    auto listNode = std::static_pointer_cast<ListNode>(node);
    std::vector<ASTNode> types;
    for (const auto &child : listNode->getChildren()) {
      auto childTypes = getOutputDataTypes(child, scope, tree);
      for (const auto &t : childTypes) {
        types.push_back(t);
      }
    }
    return types;
  } else if (node->getNodeType() == AST::Expression) {
    // TODO get this from the library
    auto typeName = resolveNodeOutDataType(node, scope, tree);
    return {std::make_shared<EntityNode>(typeName, __FILE__, __LINE__)};
  } else if (node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::Real) {
    // TODO get this from the library
    auto typeName = resolveNodeOutDataType(node, scope, tree);
    return {std::make_shared<EntityNode>(typeName, __FILE__, __LINE__)};
  } else if (node->getNodeType() == AST::Switch ||
             node->getNodeType() == AST::String) {
    // TODO get this from the library
    auto typeName = resolveNodeOutDataType(node, scope, tree);
    return {std::make_shared<EntityNode>(typeName, __FILE__, __LINE__)};
  }
  return {};
}

bool CodeAnalysis::isOutputTypeFromOutputPort(
    std::shared_ptr<DeclarationNode> funcDecl) {
  auto outputPort = ASTQuery::getModuleMainOutputPort(funcDecl);
  if (!outputPort) {
    return false;
  }
  auto outputBlockNode = outputPort->getPropertyValue("block");
  if (!outputBlockNode) {
    return false;
  }
  auto blockDecl = ASTQuery::findDeclarationByName(
      ASTQuery::getNodeName(outputBlockNode), {},
      funcDecl->getPropertyValue("blocks"));
  if (!blockDecl) {
    return false;
  }
  auto typeNode = blockDecl->getPropertyValue("type");
  return typeNode && typeNode->getNodeType() == AST::MemberAccess &&
         std::static_pointer_cast<MemberAccessNode>(typeNode)->getEntity() ==
             outputPort->getName();
}

ASTNode CodeAnalysis::getDataTypeForSignalDeclaration(
    std::shared_ptr<DeclarationNode> decl) {
  assert(decl->getEntityType() == "signal");
  auto typeNode = decl->getPropertyValue("type");
  if (typeNode) {
    if (typeNode->getNodeType() == AST::Entity) {
      return std::static_pointer_cast<EntityNode>(typeNode);
    } else if (typeNode->getNodeType() == AST::MemberAccess) {
      return typeNode;
    } else if (typeNode->getNodeType() == AST::List) {
      // option for arrays
      assert(0 == 1); // TODO implement
      return nullptr;
    } else if (typeNode->getNodeType() == AST::None) {
      return nullptr;
    }
  }
  std::cerr << __FILE__ << ":" << __LINE__
            << " ERROR unsupported object type. Will treat as 'none'"
            << std::endl;
  return nullptr;
}

std::string
CodeAnalysis::getDataTypeForDeclaration(std::shared_ptr<DeclarationNode> decl,
                                        const ScopeStack &scope, ASTNode tree) {
  if (decl->getEntityType() == "signal") {
    auto typeNode = getDataTypeForSignalDeclaration(decl);
    if (typeNode) {
      if (typeNode->getNodeType() == AST::Entity) {
        return std::static_pointer_cast<EntityNode>(typeNode)->getName();
      } else if (typeNode->getNodeType() == AST::None) {
        // FIXME we should resolve the types in CodeResolver.
        return DataTypes::Real;
      } else if (typeNode->getNodeType() == AST::MemberAccess) {
        // It's a MemberAccess, we can't fully resolve it as string, default to
        // Real for now
        return DataTypes::Real;
      }
    }
    // Default to Real if type is unspecified
    return DataTypes::Real;
  } else if (ASTQuery::isConstant(decl, scope, tree)) {
    std::shared_ptr<PropertyNode> property =
        ASTQuery::findPropertyByName(decl->getProperties(), "value");
    if (property) {
      return resolveNodeOutDataType(property->getValue(), scope, tree);
    }
  } else if (decl->getEntityType() == "string") {
    return DataTypes::String;
  } else if (decl->getEntityType() == "switch" ||
             decl->getEntityType() == "trigger") {
    return DataTypes::Switch;
  } else {
    //    qDebug() << __FILE__ << ":" << __LINE__ << " ERROR unsupported
    //    object type";
  }
  return std::string();
}

std::string CodeAnalysis::resolveEntityDataType(ASTNode node,
                                                ScopeStack scopeStack,
                                                ASTNode tree) {
  if (node->getNodeType() != AST::Entity && node->getNodeType() != AST::Array) {
    std::cerr << __FILE__ << ":" << __LINE__
              << " Error, expecting entity or array" << std::endl;
    return std::string();
  }
  auto name = ASTQuery::getNodeName(node);
  std::shared_ptr<DeclarationNode> declaration =
      std::static_pointer_cast<DeclarationNode>(
          node->getCompilerProperty("declaration"));
  if (!declaration) {
    declaration = ASTQuery::findDeclarationByName(name, scopeStack, tree);
  }
  if (declaration) {
    if (ASTQuery::isConstant(declaration, scopeStack, tree)) {
      std::vector<std::shared_ptr<PropertyNode>> properties =
          declaration->getProperties();
      std::shared_ptr<PropertyNode> property =
          ASTQuery::findPropertyByName(properties, "value");
      if (property) {
        return resolveNodeOutDataType(property->getValue(), scopeStack, tree);
      }
    } else if (declaration->getEntityType() == "signal") {
      std::vector<std::shared_ptr<PropertyNode>> properties =
          declaration->getProperties();
      ASTNode typeNode = declaration->getPropertyValue("type");
      if (typeNode && typeNode->getNodeType() == AST::Entity) {
        return std::static_pointer_cast<EntityNode>(typeNode)->getName();
      }
      return "";
    } else if (declaration->getEntityType() == "string") {
      return DataTypes::String;
    } else if (declaration->getEntityType() == "switch" ||
               declaration->getEntityType() == "trigger") {
      return DataTypes::Switch;
    } else {
      return "";
    }
  }
  return "";
}

std::string CodeAnalysis::resolveNodeOutDataType(ASTNode node,
                                                 const ScopeStack &scopeStack,
                                                 ASTNode tree) {
  if (!node)
    return "";

  std::string cacheKey;
  if (scopeStack.empty()) {
    cacheKey = "_cacheResolvedOutDataType";
  } else {
    std::string scopeKey;
    for (const auto &s : scopeStack) {
      if (s.first)
        scopeKey += ASTQuery::getNodeName(s.first) + "_";
    }
    cacheKey = "_cacheResolvedOutDataType_" + scopeKey;
  }

  auto typeProp = node->getCompilerProperty(cacheKey);
  if (typeProp && typeProp->getNodeType() == AST::String) {
    return std::static_pointer_cast<ValueNode>(typeProp)->getStringValue();
  }

  std::string result = resolveNodeOutDataTypeInternal(node, scopeStack, tree);

  if (!result.empty()) {
    node->setCompilerProperty(
        cacheKey, std::make_shared<ValueNode>(result, __FILE__, __LINE__));
  }

  return result;
}

std::string CodeAnalysis::resolveNodeOutDataTypeInternal(
    ASTNode node, const ScopeStack &scopeStack, ASTNode tree) {
  if (node->getNodeType() == AST::Int) {
    return DataTypes::Int;
  } else if (node->getNodeType() == AST::Real) {
    return DataTypes::Real;
  } else if (node->getNodeType() == AST::Switch) {
    return DataTypes::Switch;
  } else if (node->getNodeType() == AST::String) {
    return DataTypes::String;
  } else if (node->getNodeType() == AST::List) {
    return resolveListDataType(static_cast<ListNode *>(node.get()), scopeStack,
                               tree);
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    return resolveEntityDataType(node, scopeStack, tree);
  } else if (node->getNodeType() == AST::Expression) {
    return resolveExpressionDataType(static_cast<ExpressionNode *>(node.get()),
                                     scopeStack, tree);
  } else if (node->getNodeType() == AST::Range) {
    return resolveRangeDataType(static_cast<RangeNode *>(node.get()),
                                scopeStack, tree);
  } else if (node->getNodeType() == AST::GeneratorRange) {
    return DataTypes::Int;
  } else if (node->getNodeType() == AST::MemberAccess) {
    return resolveMemberAccessDataType(
        static_cast<MemberAccessNode *>(node.get()), scopeStack, tree);
  }
  return "";
}

std::string CodeAnalysis::resolveListDataType(ListNode *listnode,
                                              ScopeStack scopeStack,
                                              ASTNode tree) {
  const std::vector<ASTNode> &members = listnode->getChildren();
  if (members.size() == 0) {
    return "";
  }
  ASTNode firstMember = members.at(0);
  auto type = resolveNodeOutDataType(firstMember, scopeStack, tree);

  for (const ASTNode &member : members) {
    auto nextPortType = resolveNodeOutDataType(member, scopeStack, tree);
    if (type != nextPortType) {
      // e.g. a list with a Real becomes Real
      type = DataTypes::promote(type, nextPortType, tree);
      if (type.empty()) { // Invalid combination
        return "";
      }
    }
  }
  return type;
}

std::string CodeAnalysis::resolveExpressionDataType(ExpressionNode *exprnode,
                                                    ScopeStack scopeStack,
                                                    ASTNode tree) {
  if (!exprnode->isUnary()) {
    ASTNode left = exprnode->getLeft();
    ASTNode right = exprnode->getRight();
    auto leftType = resolveNodeOutDataType(left, scopeStack, tree);
    auto rightType = resolveNodeOutDataType(right, scopeStack, tree);

    // e.g. an expression with a Real becomes Real. Empty for an invalid
    // combination.
    if (leftType != rightType) {
      auto risk = DataTypes::promotionRisk(leftType, rightType, tree);
      if (!risk.empty()) {
        // Read by CodeValidator::findLossyConversions()
        exprnode->setCompilerProperty(
            "promotionRisk",
            std::make_shared<ValueNode>(risk, __FILE__, __LINE__));
        exprnode->setCompilerProperty(
            "promotedType",
            std::make_shared<ValueNode>(
                DataTypes::promote(leftType, rightType, tree), __FILE__,
                __LINE__));
      }
    }
    return DataTypes::promote(leftType, rightType, tree);

  } else {

    return resolveNodeOutDataType(exprnode->getValue(), scopeStack, tree);
  }
}

std::string CodeAnalysis::resolveRangeDataType(RangeNode *rangenode,
                                               const ScopeStack &scopeStack,
                                               ASTNode tree) {
  auto leftType =
      resolveNodeOutDataType(rangenode->startIndex(), scopeStack, tree);
  auto rightType =
      resolveNodeOutDataType(rangenode->endIndex(), scopeStack, tree);
  if (leftType == rightType) {
    return leftType;
  }
  return "";
}

std::string
CodeAnalysis::resolveMemberAccessDataType(MemberAccessNode *memberAccess,
                                          ScopeStack scopeStack, ASTNode tree) {
  // FIXME implement correctly. Should be read from framework?
  // Should call CodeResolver::resolveMemberAccess then get type
  if (memberAccess->getPropertyName() == "size") {
    return DataTypes::Int;
  } else if (memberAccess->getPropertyName() == "rate") {
    return DataTypes::Real;
  }
  return DataTypes::Real;
}

std::vector<ASTNode> CodeAnalysis::getDomainIOEntityDeclarations(ASTNode tree) {
  std::vector<ASTNode> decls;
  // Add domain external signals to root scope
  // They must be added before code resolver so they are available.
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "_domainDefinition" ||
          decl->getEntityType() == "gameDefinition") {
        auto inputsNode = decl->getPropertyValue("inputs");
        if (inputsNode) {
          for (const auto &inputNode : inputsNode->getChildren()) {
            if (inputNode->getNodeType() == AST::Declaration ||
                inputNode->getNodeType() == AST::ArrayDeclaration) {
              auto inputDecl =
                  std::static_pointer_cast<DeclarationNode>(inputNode);
              if (inputDecl->getEntityType() == "signal" ||
                  inputDecl->getEntityType() == "switch" ||
                  inputDecl->getEntityType() == "string") {
                decls.push_back(inputNode);
              } else {
                std::cerr << "ERROR unexpected declaration for domain input "
                             "declaration:"
                          << AST::toText(inputNode) << std::endl;
              }
            }
          }
        }
        auto outputsNode = decl->getPropertyValue("outputs");
        if (outputsNode) {
          for (const auto &outputNode : outputsNode->getChildren()) {
            if (outputNode->getNodeType() == AST::Declaration ||
                outputNode->getNodeType() == AST::ArrayDeclaration) {
              auto inputDecl =
                  std::static_pointer_cast<DeclarationNode>(outputNode);
              if (inputDecl->getEntityType() == "signal" ||
                  inputDecl->getEntityType() == "switch" ||
                  inputDecl->getEntityType() == "string") {
                decls.push_back(outputNode);
              } else {
                std::cerr << "ERROR unexpected declaration for domain output "
                             "declaration:"
                          << AST::toText(outputNode) << std::endl;
              }
            }
          }
        }
      }
    }
  }
  return decls;
}

std::shared_ptr<DeclarationNode> CodeAnalysis::matchDefinitionToTypes(
    std::vector<std::shared_ptr<DeclarationNode>> decls,
    std::shared_ptr<FunctionNode> func, const ScopeStack &scope, ASTNode tree) {
  std::shared_ptr<DeclarationNode> funcDecl;

  // Gather actual input data types from func's mainInput
  std::vector<std::string> actualTypes;
  bool hasStreamInput = false;
  auto streamInput = func ? func->getCompilerProperty("mainInput") : nullptr;
  auto resolveInputType = [&](const ASTNode &node) -> std::string {
    if (!node) {
      return "";
    }
    // 1. Inspect "typecast" compiler property first
    auto typecastProp = node->getCompilerProperty("typecast");
    if (typecastProp && typecastProp->getNodeType() == AST::String) {
      return std::static_pointer_cast<ValueNode>(typecastProp)
              ->getStringValue();
    }

    // 2. Try direct resolveNodeOutDataType
    std::string t = CodeAnalysis::resolveNodeOutDataType(node, scope, tree);
    if (!t.empty()) {
      return t;
    }

    // 3. Trace connected entity via resolveConnectionEntity
    auto connDecl =
        CodeAnalysis::resolveConnectionEntity(node, scope, tree, true);
    if (!connDecl) {
      connDecl =
          CodeAnalysis::resolveConnectionEntity(node, scope, tree, false);
    }
    if (connDecl) {
      t = CodeAnalysis::getDataTypeForDeclaration(connDecl, scope, tree);
      if (!t.empty()) {
        return t;
      }
    }

    // 4. For FunctionNode (e.g. platform modules without ports), check outputs property
    if (node->getNodeType() == AST::Function) {
      auto prevFunc = std::static_pointer_cast<FunctionNode>(node);
      auto prevDecls =
          ASTQuery::findAllDeclarations(prevFunc->getName(), scope, tree);
      for (const auto &pd : prevDecls) {
        if (pd && pd->getEntityType() == "platformModule") {
          auto outputsProp = pd->getPropertyValue("outputs");
          if (outputsProp && outputsProp->getNodeType() == AST::List &&
              !outputsProp->getChildren().empty()) {
            auto outNode = outputsProp->getChildren()[0];
            if (outNode->getNodeType() == AST::Entity) {
              t = std::static_pointer_cast<EntityNode>(outNode)->getName();
            } else if (outNode->getNodeType() == AST::String) {
              t = std::static_pointer_cast<ValueNode>(outNode)
                      ->getStringValue();
            } else {
              t = ASTQuery::getNodeName(outNode);
            }
            if (!t.empty())
              break;
          }
        }
      }
    }
    return t;
  };

  if (streamInput) {
    hasStreamInput = true;
    if (streamInput->getNodeType() == AST::List) {
      for (const auto &child : streamInput->getChildren()) {
        actualTypes.push_back(resolveInputType(child));
      }
    } else {
      actualTypes.push_back(resolveInputType(streamInput));
    }
  } else if (func) {
    // If mainInput was not set, trace upstream connection using resolveConnectionEntity
    auto connDecl =
        CodeAnalysis::resolveConnectionEntity(func, scope, tree, false);
    if (connDecl) {
      std::string t =
          CodeAnalysis::getDataTypeForDeclaration(connDecl, scope, tree);
      if (!t.empty()) {
        hasStreamInput = true;
        actualTypes.push_back(t);
      }
    }
  }

  auto scoreCandidate =
      [&](const std::shared_ptr<DeclarationNode> &decl) -> int {
    if (decl->getEntityType() != "platformModule") {
      return 0;
    }
    auto inputsNode = decl->getPropertyValue("inputs");
    if (!inputsNode) {
      return hasStreamInput ? -1 : 100;
    }
    if (!hasStreamInput) {
      return -1;
    }
    if (inputsNode->getNodeType() != AST::List) {
      return -1;
    }

    const auto &inputChildren = inputsNode->getChildren();

    std::vector<std::string> fixedExpectedTypes;
    std::shared_ptr<DeclarationNode> variadicDecl;
    for (const auto &c : inputChildren) {
      if (c && c->getNodeType() == AST::Declaration) {
        auto d = std::static_pointer_cast<DeclarationNode>(c);
        if (d->getEntityType() == "variadic" ||
            d->getEntityType() == "constrainedList") {
          variadicDecl = d;
          break;
        }
      }
      if (c->getNodeType() == AST::Entity) {
        fixedExpectedTypes.push_back(
            std::static_pointer_cast<EntityNode>(c)->getName());
      } else if (c->getNodeType() == AST::String) {
        fixedExpectedTypes.push_back(
            std::static_pointer_cast<ValueNode>(c)->getStringValue());
      } else {
        fixedExpectedTypes.push_back(ASTQuery::getNodeName(c));
      }
    }

    int score = 0;
    if (!variadicDecl) {
      if (fixedExpectedTypes.size() != actualTypes.size()) {
        return -1;
      }
      for (size_t i = 0; i < actualTypes.size(); ++i) {
        const auto &actual = actualTypes[i];
        const auto &expected = fixedExpectedTypes[i];
        if (actual.empty() || expected.empty()) {
          score += 1;
        } else if (actual == expected) {
          score += 10;
        } else if (DataTypes::implicitlyConvertible(actual, expected, tree)) {
          score += 5; // e.g. Int to Real promotion
        } else {
          return -1; // incompatible type
        }
      }
      return score;
    }

    // Variadic case
    if (actualTypes.size() < fixedExpectedTypes.size()) {
      return -1;
    }
    for (size_t i = 0; i < fixedExpectedTypes.size(); ++i) {
      const auto &actual = actualTypes[i];
      const auto &expected = fixedExpectedTypes[i];
      if (actual.empty() || expected.empty()) {
        score += 1;
      } else if (actual == expected) {
        score += 10;
      } else if (DataTypes::implicitlyConvertible(actual, expected, tree)) {
        score += 5;
      } else {
        return -1;
      }
    }

    size_t variadicCount = actualTypes.size() - fixedExpectedTypes.size();
    int minItems = 0;
    int maxItems = -1;
    auto minNode = variadicDecl->getPropertyValue("min");
    if (!minNode)
      minNode = variadicDecl->getPropertyValue("minItems");
    if (minNode && minNode->getNodeType() == AST::Int) {
      minItems = static_cast<int>(
          std::static_pointer_cast<ValueNode>(minNode)->getIntValue());
    } else if (variadicDecl->getEntityType() == "variadic") {
      minItems = 1;
    }
    auto maxNode = variadicDecl->getPropertyValue("max");
    if (!maxNode)
      maxNode = variadicDecl->getPropertyValue("maxItems");
    if (maxNode && maxNode->getNodeType() == AST::Int) {
      maxItems = static_cast<int>(
          std::static_pointer_cast<ValueNode>(maxNode)->getIntValue());
    }
    if (static_cast<int>(variadicCount) < minItems)
      return -1;
    if (maxItems >= 0 && static_cast<int>(variadicCount) > maxItems)
      return -1;

    std::vector<std::string> allowedTypes;
    auto typesNode = variadicDecl->getPropertyValue("types");
    if (typesNode && typesNode->getNodeType() == AST::List) {
      for (const auto &tc : typesNode->getChildren()) {
        if (tc->getNodeType() == AST::Entity) {
          allowedTypes.push_back(
              std::static_pointer_cast<EntityNode>(tc)->getName());
        } else if (tc->getNodeType() == AST::String) {
          allowedTypes.push_back(
              std::static_pointer_cast<ValueNode>(tc)->getStringValue());
        } else {
          allowedTypes.push_back(ASTQuery::getNodeName(tc));
        }
      }
    }

    for (size_t i = fixedExpectedTypes.size(); i < actualTypes.size(); ++i) {
      const auto &actual = actualTypes[i];
      if (allowedTypes.empty() || actual.empty()) {
        score += 1;
      } else {
        bool typeAllowed = false;
        for (const auto &allowed : allowedTypes) {
          if (actual == allowed) {
            typeAllowed = true;
            score += 10;
            break;
          } else if (DataTypes::implicitlyConvertible(actual, allowed,
                                                      tree)) {
            typeAllowed = true;
            score += 5;
            break;
          }
        }
        if (!typeAllowed)
          return -1;
      }
    }
    return score;
  };

  // First pass: score platformModule declarations and choose the best match
  int bestScore = -1;
  for (const auto &decl : decls) {
    if (decl && decl->getEntityType() == "platformModule") {
      int s = scoreCandidate(decl);
      if (s > bestScore) {
        bestScore = s;
        funcDecl = decl;
      }
    }
  }

  // Fallback pass for platformModules if none had positive score
  if (!funcDecl && actualTypes.empty()) {
    for (const auto &decl : decls) {
      if (decl && decl->getEntityType() == "platformModule") {
        auto inputs = decl->getPropertyValue("inputs");
        bool matches = true;
        if (inputs) {
          if (!hasStreamInput)
            continue;
          if (inputs->getNodeType() != AST::List)
            matches = false;
        }
        if (matches) {
          funcDecl = decl;
          break;
        }
      }
    }
  }

  // Second pass: fallback to regular module declaration
  if (!funcDecl) {
    for (const auto &decl : decls) {
      funcDecl = decl;
      break;
    }
  }

  assert(!funcDecl || funcDecl->getName() == func->getName() ||
         funcDecl->getName().find(func->getName() + "@") == 0);
  return funcDecl;
}

CodeAnalysis::TypeTree CodeAnalysis::getTypeTreeForFunctionInstance(
    std::shared_ptr<FunctionNode> func, const ScopeStack &scope, ASTNode tree,
    int &instanceCounter) {
  CodeAnalysis::TypeTree typeTree;

  return typeTree;
}

CodeAnalysis::NodeRole CodeAnalysis::determineNodeRole(
    ASTNode entity, std::shared_ptr<DeclarationNode> funcDecl,
    const ScopeStack &scope, ASTNode tree,
    std::shared_ptr<DeclarationNode> &outEntityDecl) {
  auto entityName = ASTQuery::getNodeName(entity);
  auto funcType = ASTQuery::findTypeDeclaration(funcDecl, scope, tree);

  if (ASTQuery::isCodeGenerator(funcType, scope, tree) &&
      ASTQuery::isCallable(funcType, scope, tree)) {
    auto blocksNode = funcDecl->getPropertyValue("blocks");
    std::vector<ASTNode> blocksList =
        (blocksNode && blocksNode->getNodeType() == AST::List)
            ? blocksNode->getChildren()
            : std::vector<ASTNode>();

    auto portsNode = funcDecl->getPropertyValue("ports");
    if (portsNode && portsNode->getNodeType() == AST::List) {
      for (const auto &portNode : portsNode->getChildren()) {
        if (portNode->getNodeType() == AST::Declaration) {
          auto portDecl = std::static_pointer_cast<DeclarationNode>(portNode);
          auto portBlock = portDecl->getPropertyValue("block");
          if (portBlock && ASTQuery::getNodeName(portBlock) == entityName) {
            outEntityDecl = ASTQuery::findDeclarationByName(
                entityName, {{nullptr, blocksList}}, nullptr);
            const std::string &entityType = portDecl->getEntityType();
            if (entityType == "mainOutputPort" ||
                entityType == "propertyOutputPort" ||
                entityType == "secondaryOutputPort") {
              return NodeRole::Output;
            } else if (entityType == "mainInputPort" ||
                       entityType == "propertyInputPort" ||
                       entityType == "secondaryInputPort") {
              return NodeRole::Input;
            }
          }
        }
      }
    }

    // Entities in functions/modules are local unless marked as persistent
    if (scope.size() > 0) {
      ScopeStack localScope = {scope.back()};
      auto entityDecl = ASTQuery::findDeclarationByName(
          ASTQuery::getNodeName(entity), localScope, nullptr);
      if (entityDecl) { // If in local scope it is an internal entity
        outEntityDecl = entityDecl;
        auto persistentProp = entityDecl->getCompilerProperty("persistent");
        auto explicitPersistentProp = entityDecl->getPropertyValue("persistent");
        auto resetProp = entityDecl->getPropertyValue("reset");
        if (persistentProp ||
            (explicitPersistentProp &&
             explicitPersistentProp->getNodeType() != AST::None) ||
            (resetProp && resetProp->getNodeType() != AST::None)) {
          return NodeRole::Persistent;
        } else {
          return NodeRole::Internal;
        }
      }
    }

    if (ASTQuery::isDomainMember(funcType, scope, tree)) {
      ScopeStack outerScope = scope;
      if (outerScope.size() > 0) {
        outerScope.pop_back();
      }
      auto entityDecl = ASTQuery::findDeclarationByName(
          ASTQuery::getNodeName(entity), outerScope, tree);
      if (entityDecl) { // If in scope it is an external entity
        outEntityDecl = entityDecl;
        return NodeRole::External;
      }
    }
  } else {
    std::cerr << "Unexpected function type: " << funcDecl->toText()
              << std::endl;
  }
  return NodeRole::Unknown;
}

void CodeAnalysis::processStreamNode(ASTNode streamNode,
                                     const ScopeStack &scope, ASTNode tree,
                                     CodeAnalysis::TypeTree &typeTree) {
  if (streamNode->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(streamNode);
    CodeAnalysis::TypeTree functionTree;
    functionTree.instance = func;
    auto funcDecl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(func),
                                                    scope, tree);
    auto funcDeclType = ASTQuery::findTypeDeclaration(funcDecl, scope, tree);
    if (funcDecl && funcDeclType) {
      if (funcDecl->getEntityType() != "platformModule") {
        auto blocks = funcDecl->getPropertyValue("blocks");
        auto streams = funcDecl->getPropertyValue("streams");

        ScopeStack funcScope;
        if (ASTQuery::isDomainMember(funcDeclType, scope, tree)) {
          // Reactions and loops are domain members an have access to the parent
          // Scope and root scope
          if (scope.size() > 0) {
            funcScope.push_back(scope.back());
          }
        }
        funcScope.push_back(
            {func, blocks ? blocks->getChildren() : std::vector<ASTNode>{}});
        auto streamBlocks =
            ASTQuery::getContainedStreamBlocks(funcDecl, funcScope, tree);
        for (const auto &block : streamBlocks) {
          for (const auto &stream : block.streams) {
            StreamNodeIterator it(stream);
            while (it.hasNext()) {
              processStreamNode(it.next(), funcScope, tree, functionTree);
            }
          }
        }
        if (ASTQuery::isDomainMember(funcDeclType, scope, tree)) {
          // This node could be using blocks from the parent which are not used
          // by the parent. These will show up as external, and should be added
          // to the parent in the right place
          for (const auto &external : functionTree.external) {
            if (!typeTree.contains(ASTQuery::getNodeName(external.first))) {
              // FIXME adjust scope for parent
              auto parentDecl = ASTQuery::findDeclarationByName(
                  ASTQuery::getNodeName(typeTree.instance), scope, tree);
              if (parentDecl) {
                if (external.first->getNodeType() == AST::Declaration) {
                  if (ASTQuery::isInputPort(
                          std::static_pointer_cast<DeclarationNode>(
                              external.first),
                          parentDecl, scope, tree)) {
                    typeTree.input.push_back(external);

                  } else if (ASTQuery::isOutputPort(
                                 std::static_pointer_cast<DeclarationNode>(
                                     external.first),
                                 parentDecl, scope, tree)) {
                    typeTree.output.push_back(external);

                  } else {
                    auto blocks = ASTQuery::getModuleEntities(
                        std::static_pointer_cast<DeclarationNode>(parentDecl));
                    for (const auto &block : blocks) {
                      if (ASTQuery::getNodeName(block) ==
                          ASTQuery::getNodeName(external.first)) {
                        typeTree.internal.push_back(external);
                        break;
                      }
                    }
                  }
                } // closes if (external.first->getNodeType() ==
                  // AST::Declaration)
              }
            }
          }
        }
      }
    }
    auto domainName = CodeAnalysis::getNodeDomainName(func, scope, tree);
    auto *domainTree = typeTree.getDomainRootTree(domainName);
    if (domainTree) {
      domainTree->nodes.push_back(functionTree);
    } else {
      std::cerr << __FILE__ << ":" << __LINE__
                << " ERROR: domain not found: " << std::endl;
    }
  } else if (streamNode->getNodeType() == AST::Entity ||
             streamNode->getNodeType() == AST::Array) {
    auto entityNode = streamNode;
    // Check if declared in tree root
    auto entityName = ASTQuery::getNodeName(entityNode);
    std::shared_ptr<DeclarationNode> entityDecl;
    if (scope.size() > 0) {
      auto context = scope.back().first;
      if (context->getNodeType() == AST::Function) {
        auto func = std::static_pointer_cast<FunctionNode>(context);
        auto funcDecl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(func), scope, tree);
        auto funcType = ASTQuery::findTypeDeclaration(funcDecl, scope, tree);

        // TODO we should sort the inputs and outputs not by order of appearance
        // in streams, but by order of port declarations. Perhaps order to
        // optmize memory access

        if (ASTQuery::isCodeGenerator(funcType, scope, tree) &&
            ASTQuery::isCallable(funcType, scope, tree)) {

          std::shared_ptr<DeclarationNode> outEntityDecl;
          NodeRole role =
              determineNodeRole(entityNode, funcDecl, scope, tree, outEntityDecl);

          if (role == NodeRole::Output) {
            auto type = resolveEntityDataType(entityNode, {}, tree);
            if (!typeTree.contains(entityName)) {
              typeTree.output.push_back({outEntityDecl, type});
            }
            return;
          } else if (role == NodeRole::Input) {
            auto type = resolveEntityDataType(entityNode, {}, tree);
            if (!typeTree.contains(entityName)) {
              typeTree.input.push_back({outEntityDecl, type});
            }
            return;
          } else if (role == NodeRole::External) {
            auto type = resolveEntityDataType(entityNode, scope, tree);
            if (!typeTree.contains(entityName)) {
              typeTree.external.push_back({outEntityDecl, type});
            }
            return;
          } else if (role == NodeRole::Persistent) {
            auto type = resolveEntityDataType(entityNode, scope, tree);
            if (!typeTree.contains(entityName)) {
              typeTree.persistent.push_back({outEntityDecl, type});
            }
            return;
          } else if (role == NodeRole::Internal) {
            auto type = resolveEntityDataType(entityNode, scope, tree);
            if (!typeTree.contains(entityName)) {
              typeTree.internal.push_back({outEntityDecl, type});
            }
            return;
          }

        } else {
          std::cerr << "Unexpected function type: " << funcDecl->toText()
                    << std::endl;
          // What is this function?!
        }
      }
    }

    if (entityDecl) { // check if global
      auto domainName = CodeAnalysis::getNodeDomainName(entityDecl, scope, tree);
      auto *domainTree = typeTree.getDomainRootTree(domainName);
      if (domainTree) {
        auto type = resolveEntityDataType(entityNode, {}, tree);
        if (!domainTree->contains(entityName)) {
          domainTree->internal.push_back({entityDecl, type});
        }
      } else {
        std::cerr << __FILE__ << ":" << __LINE__ << " ERROR: domain not found"
                  << std::endl;
      }

    } else {
      // Check if declared in scope
      entityDecl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(entityNode),
                                                   scope, tree);

      auto domainName = CodeAnalysis::getNodeDomainName(entityDecl, scope, tree);
      auto *domainTree = typeTree.getDomainRootTree(domainName);
      if (domainTree) {
        auto type = resolveEntityDataType(entityNode, {}, tree);
        if (!domainTree->contains(entityName)) {
          domainTree->internal.push_back({entityDecl, type});
        }
      } else {
        std::cerr << __FILE__ << ":" << __LINE__ << " ERROR: domain not found"
                  << std::endl;
      }
    }
    if (streamNode->getNodeType() == AST::Array) {
      auto indeces = std::static_pointer_cast<ArrayNode>(streamNode)->index();
      if (indeces) {
        for (const auto &index : indeces->getChildren()) {
          processStreamNode(index, scope, tree, typeTree);
        }
      }
    }
  } else if (streamNode->getNodeType() == AST::List) {
    for (const auto &listNode : streamNode->getChildren()) {
      processStreamNode(listNode, scope, tree, typeTree);
    }
  } else if (streamNode->getNodeType() == AST::Expression) {
    auto exprNode = std::static_pointer_cast<ExpressionNode>(streamNode);
    processStreamNode(exprNode->getLeft(), scope, tree, typeTree);
    processStreamNode(exprNode->getRight(), scope, tree, typeTree);
  } else if (streamNode->getNodeType() == AST::MemberAccess) {
    if (scope.size() == 0) {
      std::cerr << __FILE__ << ":" << __LINE__
                << " Error: member access in root scope: "
                << streamNode->toText() << std::endl;
      return;
    }
    typeTree.external.push_back({streamNode, DataTypes::Int});
  }
}

CodeAnalysis::TypeTree
CodeAnalysis::getStateStructInformation(const ScopeStack &scope, ASTNode tree) {
  CodeAnalysis::TypeTree typeTree;

  std::vector<ASTNode> domainInstances;
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "_domainDefinition" ||
          decl->getEntityType() == "gameDefinition") {
        typeTree.nodes.emplace_back(TypeTree());
        auto &domainTree = typeTree.nodes.back();
        domainTree.instance = decl;
        auto outputsList = decl->getPropertyValue("outputs");
        if (outputsList) {
          for (const auto &domainNode : outputsList->getChildren()) {
            if (domainNode->getNodeType() == AST::Declaration ||
                domainNode->getNodeType() == AST::ArrayDeclaration) {
              auto blockDecl =
                  std::static_pointer_cast<DeclarationNode>(domainNode);
              auto type = getDataTypeForDeclaration(blockDecl, {}, tree);
              if (!domainTree.contains(blockDecl->getName())) {
                domainTree.external.push_back({blockDecl, type});
              }
            }
          }
        }
        auto inputsList = decl->getPropertyValue("inputs");
        if (inputsList) {
          for (const auto &domainNode : inputsList->getChildren()) {
            if (domainNode->getNodeType() == AST::Declaration ||
                domainNode->getNodeType() == AST::ArrayDeclaration) {
              auto blockDecl =
                  std::static_pointer_cast<DeclarationNode>(domainNode);
              auto type = getDataTypeForDeclaration(blockDecl, {}, tree);
              if (!domainTree.contains(blockDecl->getName())) {
                domainTree.external.push_back({blockDecl, type});
              }
            }
          }
        }
      }
    }
  }

  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      StreamNodeIterator it(std::static_pointer_cast<StreamNode>(node));
      while (it.hasNext()) {
        processStreamNode(it.next(), scope, tree, typeTree);
      }
    } else if (node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration) {
    }
  }
  return typeTree;
}

void CodeAnalysis::processStreamNodeForDeclaration(
    ASTNode node, std::shared_ptr<DeclarationNode> funcDecl,
    const ScopeStack &funcScope, ASTNode tree,
    CodeAnalysis::TypeTree &typeTree) {
  if (!node)
    return;
  if (node->getNodeType() == AST::Function) {
    auto nestedFunc = std::static_pointer_cast<FunctionNode>(node);
    auto decls =
        ASTQuery::findAllDeclarations(nestedFunc->getName(), funcScope, tree);
    auto nestedFuncDecl = CodeAnalysis::matchDefinitionToTypes(
        decls, nestedFunc, funcScope, tree);
    if (!nestedFuncDecl) {
      nestedFuncDecl = ASTQuery::findDeclarationByName(nestedFunc->getName(),
                                                       funcScope, tree);
    }
    CodeAnalysis::TypeTree nestedTree;
    nestedTree.instance = nestedFunc; // Set instance to the call site
    if (nestedFuncDecl && nestedFuncDecl->getEntityType() != "platformModule") {
      nestedTree = getStateStructInformationForDeclaration(nestedFuncDecl,
                                                           funcScope, tree);
      nestedTree.instance = nestedFunc; // Set instance to the call site
    }
    typeTree.nodes.push_back(nestedTree);
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    std::shared_ptr<DeclarationNode> outBlockDecl;
    NodeRole role =
        determineNodeRole(node, funcDecl, funcScope, tree, outBlockDecl);
    if (role == NodeRole::External && outBlockDecl) {
      std::string blockName = ASTQuery::getNodeName(outBlockDecl);
      if (!typeTree.contains(blockName)) {
        auto type = getDataTypeForDeclaration(outBlockDecl, funcScope, tree);
        typeTree.external.push_back({outBlockDecl, type});
      }
    }
    if (node->getNodeType() == AST::Array) {
      auto indeces = std::static_pointer_cast<ArrayNode>(node)->index();
      if (indeces) {
        for (const auto &index : indeces->getChildren()) {
          processStreamNodeForDeclaration(index, funcDecl, funcScope, tree,
                                          typeTree);
        }
      }
    }
  } else if (node->getNodeType() == AST::Stream) {
    StreamNodeIterator it(std::static_pointer_cast<StreamNode>(node));
    while (it.hasNext()) {
      processStreamNodeForDeclaration(it.next(), funcDecl, funcScope, tree,
                                      typeTree);
    }
  } else if (node->getNodeType() == AST::Expression) {
    auto exprNode = std::static_pointer_cast<ExpressionNode>(node);
    if (exprNode->isUnary()) {
      processStreamNodeForDeclaration(exprNode->getValue(), funcDecl, funcScope,
                                      tree, typeTree);
    } else {
      processStreamNodeForDeclaration(exprNode->getLeft(), funcDecl, funcScope,
                                      tree, typeTree);
      processStreamNodeForDeclaration(exprNode->getRight(), funcDecl, funcScope,
                                      tree, typeTree);
    }
  } else if (node->getNodeType() == AST::List) {
    for (const auto &child : node->getChildren()) {
      processStreamNodeForDeclaration(child, funcDecl, funcScope, tree,
                                      typeTree);
    }
  }
}

CodeAnalysis::TypeTree CodeAnalysis::getStateStructInformationForDeclaration(
    std::shared_ptr<DeclarationNode> funcDecl, const ScopeStack &scope,
    ASTNode tree) {
  CodeAnalysis::TypeTree typeTree;
  typeTree.instance = funcDecl;

  if (!funcDecl)
    return typeTree;

  auto blocks = funcDecl->getPropertyValue("blocks");
  ScopeStack funcScope = scope;
  funcScope.push_back(
      {funcDecl, blocks ? blocks->getChildren() : std::vector<ASTNode>()});

  if (blocks && blocks->getNodeType() == AST::List) {
    for (const auto &blockNode : blocks->getChildren()) {
      if (blockNode->getNodeType() == AST::Declaration ||
          blockNode->getNodeType() == AST::ArrayDeclaration) {
        auto blockDecl = std::static_pointer_cast<DeclarationNode>(blockNode);
        std::shared_ptr<DeclarationNode> outBlockDecl;
        NodeRole role = determineNodeRole(blockNode, funcDecl, funcScope, tree,
                                          outBlockDecl);
        if (role == NodeRole::Persistent) {
          auto type = getDataTypeForDeclaration(blockDecl, funcScope, tree);
          typeTree.persistent.push_back({blockDecl, type});
        } else if (role == NodeRole::Internal) {
          auto type = getDataTypeForDeclaration(blockDecl, funcScope, tree);
          typeTree.internal.push_back({blockDecl, type});
        } else if (role == NodeRole::External) {
          auto type = getDataTypeForDeclaration(blockDecl, funcScope, tree);
          typeTree.external.push_back({blockDecl, type});
        } else if (role == NodeRole::Input) {
          auto type = getDataTypeForDeclaration(blockDecl, funcScope, tree);
          typeTree.input.push_back({blockDecl, type});
        } else if (role == NodeRole::Output) {
          auto type = getDataTypeForDeclaration(blockDecl, funcScope, tree);
          typeTree.output.push_back({blockDecl, type});
        }
      }
    }
  }

  auto streamBlocks =
      ASTQuery::getContainedStreamBlocks(funcDecl, funcScope, tree);
  for (const auto &block : streamBlocks) {
    for (const auto &stream : block.streams) {
      processStreamNodeForDeclaration(stream, funcDecl, funcScope, tree,
                                      typeTree);
    }
  }
  return typeTree;
}

bool CodeAnalysis::TypeTree::contains(std::string name) {
  if (namesSet.find(name) != namesSet.end()) {
    return true;
  }
  for (const auto &elem : output) {
    auto elemName = ASTQuery::getNodeName(elem.first);
    namesSet.insert(elemName);
    if (elemName == name) {
      return true;
    }
  }
  for (const auto &elem : input) {
    auto elemName = ASTQuery::getNodeName(elem.first);
    namesSet.insert(elemName);
    if (elemName == name) {
      return true;
    }
  }
  for (const auto &elem : external) {
    auto elemName = ASTQuery::getNodeName(elem.first);
    namesSet.insert(elemName);
    if (elemName == name) {
      return true;
    }
  }
  for (const auto &elem : internal) {
    auto elemName = ASTQuery::getNodeName(elem.first);
    namesSet.insert(elemName);
    if (elemName == name) {
      return true;
    }
  }
  for (const auto &elem : persistent) {
    auto elemName = ASTQuery::getNodeName(elem.first);
    namesSet.insert(elemName);
    if (elemName == name) {
      return true;
    }
  }
  return false;
}

CodeAnalysis::TypeTree *
CodeAnalysis::TypeTree::find(ASTNode node, CodeAnalysis::TypeTree *tree) {
  if (!node) {
    return nullptr;
  }
  if (!tree) {
    tree = this;
  }
  if (tree->instance == node) {
    return tree;
  }
  for (auto &innerTree : tree->nodes) {
    auto *foundTree = find(node, &innerTree);
    if (foundTree) {
      return foundTree;
    }
  }
  return nullptr;
}

CodeAnalysis::TypeTree *
CodeAnalysis::TypeTree::getParentTreeForNode(ASTNode node,
                                             CodeAnalysis::TypeTree *tree) {
  if (!tree) {
    tree = this;
  }
  for (auto &childTree : tree->nodes) {
    if (childTree.instance == node) {
      return tree;
    }
    auto *foundTree = getParentTreeForNode(node, &childTree);
    if (foundTree) {
      return foundTree;
    }
  }
  return nullptr;
}

CodeAnalysis::TypeTree *
CodeAnalysis::TypeTree::getDomainRootTree(std::string domainName) {
  for (auto &tree : this->nodes) {
    if (tree.instance) {
      if (tree.instance->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(tree.instance);
        if (decl->getEntityType() == "_domainDefinition" ||
            decl->getEntityType() == "gameDefinition") {
          if (decl->getName() == domainName) {
            return &tree;
          }
        }
      }
    }
  }
  return this;
}
