/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "rateandtyperesolver.hpp"

#include "stride/parser/entitynode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <map>

using namespace strd;

void RateAndTypeResolver::setPortEntityAttributes(const ScopeStack &scope,
                                                  const ASTNode &tree) {
  for (const ASTNode &node : m_ctx.tree->getChildren()) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_ctx.tree);
    if (!ASTQuery::isCodeGenerator(typeDecl, scope, tree)) {
      continue;
    }
    setPortEntityAttributesForDeclaration(decl);
    if (auto entities = decl->getPropertyValue("entities")) {
      for (const auto &child : entities->getChildren()) {
        if (!child || child->getNodeType() != AST::Declaration) {
          continue;
        }
        auto childDecl = std::static_pointer_cast<DeclarationNode>(child);
        auto childTypeDecl =
            ASTQuery::findTypeDeclaration(childDecl, {}, m_ctx.tree);
        if (ASTQuery::isCodeGenerator(childTypeDecl, scope, tree)) {
          setPortEntityAttributesForDeclaration(childDecl);
        }
      }
    }
  }
}

void RateAndTypeResolver::setPortEntityAttributesForDeclaration(
    const std::shared_ptr<DeclarationNode> &decl) {
  ASTNode entities = decl->getPropertyValue("entities");
  ScopeStack entScope;
  if (entities && entities->getNodeType() == AST::List) {
    entScope.push_back({decl, entities->getChildren()});
  }
  auto applyForPort = [&](const std::shared_ptr<DeclarationNode> &portNode) {
    if (!portNode) {
      return;
    }
    auto portEntityNode = portNode->getPropertyValue("entity");
    if (portEntityNode && portEntityNode->getNodeType() == AST::Entity) {
      if (auto entityDecl =
              ASTQuery::findDeclarationForNode(portEntityNode, entScope, nullptr)) {
        setPortAttributesForEntity(entityDecl, portNode->getName());
      }
    }
  };
  applyForPort(ASTQuery::getModuleMainInputPort(decl));
  applyForPort(ASTQuery::getModuleMainOutputPort(decl));
}

void RateAndTypeResolver::setPortAttributesForEntity(
    const std::shared_ptr<DeclarationNode> &entityDecl,
    const std::string &portName) {
  if (!ASTQuery::getDataTypeClassNames(entityDecl, m_ctx.tree).empty()) {
    auto type = entityDecl->getPropertyValue("type");
    if (!type || type->getNodeType() == AST::None) {
      entityDecl->setPropertyValue(
          "type", std::make_shared<MemberAccessNode>(portName, "type", __FILE__,
                                                     __LINE__));
    }
  }
  if (ASTQuery::isRated(entityDecl, m_ctx.tree)) {
    auto rate = entityDecl->getPropertyValue("rate");
    if (!rate || rate->getNodeType() == AST::None ||
        (rate->getNodeType() == AST::Int &&
         std::static_pointer_cast<ValueNode>(rate)->getIntValue() < 0)) {
      entityDecl->setPropertyValue(
          "rate", std::make_shared<MemberAccessNode>(portName, "rate", __FILE__,
                                                     __LINE__));
    }
  }
}

void RateAndTypeResolver::resolveRatesAndTypes(const ScopeStack &scope,
                                               const ASTNode &tree) {
  std::vector<ASTNode> children = m_ctx.tree->getChildren();

  for (const ASTNode &node : children) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_ctx.tree);
    if (!ASTQuery::isCodeGenerator(typeDecl, scope, tree)) {
      continue;
    }
    auto streams = decl->getPropertyValue("streams");
    auto entities = decl->getPropertyValue("entities");
    ScopeStack localScope = scope;
    if (entities && entities->getNodeType() == AST::List) {
      localScope.push_back({decl, entities->getChildren()});
    }
    if (!streams) {
      continue;
    }
    const auto &streamsChildren = streams->getChildren();
    for (auto it = streamsChildren.rbegin(); it != streamsChildren.rend();
         ++it) {
      if (*it && (*it)->getNodeType() == AST::Stream) {
        resolveStreamTypesReverse(std::static_pointer_cast<StreamNode>(*it),
                                  localScope, tree);
      }
    }
    for (const auto &streamNode : streamsChildren) {
      if (streamNode && streamNode->getNodeType() == AST::Stream) {
        resolveStreamTypesForward(
            std::static_pointer_cast<StreamNode>(streamNode), localScope, tree);
      }
    }
  }

  for (const ASTNode &node : children) {
    if (node && node->getNodeType() == AST::Stream) {
      resolveStreamRates(std::static_pointer_cast<StreamNode>(node), scope,
                         tree, false);
    }
  }

  for (auto rit = children.rbegin(); rit != children.rend(); ++rit) {
    if (*rit && (*rit)->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(*rit);
      resolveStreamTypesReverse(stream, scope, tree);
      resolveStreamRatesReverse(stream, scope, tree);
    }
  }

  for (const ASTNode &node : children) {
    if (node && node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      resolveStreamTypesForward(stream, scope, tree);
      resolveStreamRatesReverse(stream, scope, tree);
      resolveStreamRates(stream, scope, tree);
      resolveStreamRatesReverse(stream, scope, tree);
    }
  }
}

void RateAndTypeResolver::resolveStreamRatesReverse(
    const std::shared_ptr<StreamNode> &stream, const ScopeStack &scope,
    const ASTNode &tree) {
  if (!stream) {
    return;
  }
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  if (!right) {
    return;
  }
  double rate = CodeAnalysis::getNodeRate(left, scope, tree);
  double rightRate = -1.0;
  ASTNode rightTarget = (right->getNodeType() == AST::Stream)
                            ? static_cast<StreamNode *>(right.get())->getLeft()
                            : right;
  if (right->getNodeType() == AST::Stream) {
    resolveStreamRatesReverse(std::static_pointer_cast<StreamNode>(right),
                              scope, tree);
  }
  if (rightTarget && rightTarget->getNodeType() == AST::Function) {
    double inPortRate = CodeAnalysis::getModulePortRate(
        std::static_pointer_cast<FunctionNode>(rightTarget), "mainInputPort",
        scope, tree);
    if (inPortRate >= 0) {
      rightRate = inPortRate;
    }
  }
  if (rightRate < 0 && rightTarget) {
    rightRate = CodeAnalysis::getNodeRate(rightTarget, scope, tree);
  }
  if (rate < 0 && rightRate >= 0) {
    std::string leftDom = CodeAnalysis::getNodeDomainName(left, scope, m_ctx.tree);
    std::string rightDom =
        CodeAnalysis::getNodeDomainName(rightTarget, scope, m_ctx.tree);
    if (leftDom.empty() || rightDom.empty() || leftDom == rightDom) {
      setNodeRate(left, rightRate, scope, tree);
    }
  }
}

void RateAndTypeResolver::resolveStreamRates(
    const std::shared_ptr<StreamNode> &stream, const ScopeStack &scope,
    const ASTNode &tree, bool fallbackToDomainDefault) {
  if (!stream) {
    return;
  }
  auto resolveDefaultDomainRate = [&](const ASTNode &node) -> double {
    ASTNode domNode = CodeAnalysis::getNodeDomain(node, scope, m_ctx.tree);
    if (!domNode) {
      domNode = CodeAnalysis::getNodeDomain(stream, scope, m_ctx.tree);
    }
    if (domNode) {
      auto domainDecl = ASTQuery::findDomainDeclaration(
          ASTQuery::getDomainIdentifier(domNode, scope, m_ctx.tree),
          m_ctx.tree);
      if (domainDecl) {
        return ASTQuery::getDomainDefaultRate(domainDecl);
      }
    }
    if (m_ctx.system && m_ctx.system->getPlatformDomain()) {
      auto domainDecl = ASTQuery::findDomainDeclaration(
          ASTQuery::getNodeName(m_ctx.system->getPlatformDomain()), m_ctx.tree);
      if (domainDecl) {
        return ASTQuery::getDomainDefaultRate(domainDecl);
      }
    }
    return -1.0;
  };

  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  double rate = CodeAnalysis::getNodeRate(left, scope, m_ctx.tree);
  if (rate < 0) {
    if (fallbackToDomainDefault) {
      double defaultRate = resolveDefaultDomainRate(left);
      if (defaultRate > 0) {
        rate = defaultRate;
        setNodeRate(left, rate, scope, m_ctx.tree);
      }
    }
  } else {
    setNodeRate(left, rate, scope, m_ctx.tree);
  }
  if (!right) {
    return;
  }

  auto propagateToRightTarget = [&](const ASTNode &target) {
    double targetRate = CodeAnalysis::getNodeRate(target, scope, m_ctx.tree);
    bool targetAdaptsInputRate =
        target && target->getNodeType() == AST::Function &&
        CodeAnalysis::getModulePortRate(
            std::static_pointer_cast<FunctionNode>(target), "mainInputPort",
            scope, m_ctx.tree) >= 0;
    if (targetRate < 0) {
      if (rate >= 0 && !targetAdaptsInputRate) {
        std::string leftDom =
            CodeAnalysis::getNodeDomainName(left, scope, m_ctx.tree);
        std::string targetDom =
            CodeAnalysis::getNodeDomainName(target, scope, m_ctx.tree);
        if (leftDom.empty() || targetDom.empty() || leftDom == targetDom) {
          setNodeRate(target, rate, scope, m_ctx.tree);
          return;
        }
      }
      if (fallbackToDomainDefault) {
        double defaultRate = resolveDefaultDomainRate(target);
        if (defaultRate > 0) {
          setNodeRate(target, defaultRate, scope, m_ctx.tree);
        }
      }
    } else if (right->getNodeType() != AST::Stream) {
      setNodeRate(target, targetRate, scope, m_ctx.tree);
    }
  };

  if (right->getNodeType() == AST::Stream) {
    auto rightStream = std::static_pointer_cast<StreamNode>(right);
    propagateToRightTarget(rightStream->getLeft());
    resolveStreamRates(rightStream, scope, tree, fallbackToDomainDefault);
  } else {
    propagateToRightTarget(right);
  }
}

void RateAndTypeResolver::resolveStreamTypesReverse(
    const std::shared_ptr<StreamNode> &stream, const ScopeStack &stack,
    const ASTNode &tree) {
  if (!stream) {
    return;
  }
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  if (!right) {
    return;
  }
  auto types = CodeAnalysis::getOutputDataTypes(left, stack, tree);
  std::vector<ASTNode> rightTypes;
  if (right->getNodeType() == AST::Stream) {
    auto rightStream = std::static_pointer_cast<StreamNode>(right);
    resolveStreamTypesReverse(rightStream, stack, tree);
    rightTypes = CodeAnalysis::getInputDataTypes(rightStream->getLeft(), stack,
                                                 tree);
  } else {
    rightTypes = CodeAnalysis::getInputDataTypes(right, stack, tree);
  }
  if (rightTypes.size() == types.size()) {
    for (size_t i = 0; i < rightTypes.size(); ++i) {
      if (rightTypes[i] && rightTypes[i]->getNodeType() != AST::None) {
        setNodeType(left, rightTypes[i], stack, tree);
      }
    }
  }
}

void RateAndTypeResolver::markUnresolvedOutputType(const ASTNode &source,
                                                   const ASTNode &target,
                                                   const ScopeStack &scope,
                                                   const ASTNode &tree) {
  if (!source || !target || source->getNodeType() != AST::Function ||
      (target->getNodeType() != AST::Entity &&
       target->getNodeType() != AST::Array)) {
    return;
  }
  auto funcDecl = ASTQuery::findDeclarationForNode(source, scope, tree);
  if (!funcDecl) {
    funcDecl = ASTQuery::findDeclarationForNode(source, {}, m_ctx.tree);
  }
  if (!funcDecl || !ASTQuery::isOutputTypeFromOutputPort(funcDecl)) {
    return;
  }
  auto targetDecl = ASTQuery::findDeclarationForNode(target, scope, tree);
  if (!targetDecl) {
    targetDecl = ASTQuery::findDeclarationForNode(target, {}, m_ctx.tree);
  }
  if (!targetDecl || !targetDecl->getCompilerProperty("autoDeclared")) {
    return;
  }
  auto type = targetDecl->getPropertyValue("type");
  if (!type || type->getNodeType() == AST::None) {
    targetDecl->setCompilerProperty(
        "unresolvedType",
        std::make_shared<ValueNode>(funcDecl->getName(), __FILE__, __LINE__));
  }
}

void RateAndTypeResolver::resolveStreamTypesForward(
    const std::shared_ptr<StreamNode> &stream, const ScopeStack &scope,
    const ASTNode &tree) {
  if (!stream) {
    return;
  }
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  if (!right) {
    return;
  }
  auto types = CodeAnalysis::getOutputDataTypes(left, scope, tree);

  if (right->getNodeType() == AST::Stream) {
    auto rightStream = std::static_pointer_cast<StreamNode>(right);
    markUnresolvedOutputType(left, rightStream->getLeft(), scope, tree);
    auto rightTypes =
        CodeAnalysis::getInputDataTypes(rightStream->getLeft(), scope, tree);
    if (rightTypes.size() == types.size()) {
      for (size_t i = 0; i < rightTypes.size(); ++i) {
        if (!rightTypes[i] || rightTypes[i]->getNodeType() == AST::None) {
          setNodeType(rightStream->getLeft(), types[i], scope, tree);
        }
      }
    }
    resolveStreamTypesForward(rightStream, scope, tree);
  } else {
    markUnresolvedOutputType(left, right, scope, tree);
    auto rightTypes = CodeAnalysis::getInputDataTypes(right, scope, tree);
    if (rightTypes.size() == types.size()) {
      if (right->getNodeType() == AST::List) {
        if (right->getChildren().size() == types.size()) {
          for (size_t i = 0; i < rightTypes.size(); ++i) {
            setNodeType(right->getChildren()[i], types[i], scope, tree);
          }
        }
      } else if (right->getNodeType() == AST::Expression) {
        for (size_t i = 0; i < right->getChildren().size() && i < types.size();
             ++i) {
          setNodeType(right->getChildren()[i], types[i], scope, tree);
        }
      } else {
        if (types.size() == 1 && types[0] != nullptr) {
          setNodeType(right, types[0], scope, tree);
        } else if (right->getChildren().size() == types.size()) {
          for (size_t i = 0; i < types.size(); ++i) {
            setNodeType(right->getChildren()[i], types[i], scope, tree);
          }
        }
      }
    }
  }
}

void RateAndTypeResolver::setNodeRate(const ASTNode &node, double rate,
                                      const ScopeStack &scope,
                                      const ASTNode &tree) {
  if (!node || rate < 0) {
    return;
  }
  node->setCompilerProperty("rate",
                            std::make_shared<ValueNode>(rate, __FILE__, __LINE__));
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    auto declaration = ASTQuery::findDeclarationForNode(node, scope, tree);
    if (declaration && ASTQuery::isRated(declaration, m_ctx.tree)) {
      if (CodeAnalysis::getNodeRate(declaration, scope, tree) < 0) {
        declaration->setCompilerProperty(
            "ratePropagated",
            std::make_shared<ValueNode>(true, __FILE__, __LINE__));
        declaration->setPropertyValue(
            "rate", std::make_shared<ValueNode>(rate, __FILE__, __LINE__));
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    std::static_pointer_cast<FunctionNode>(node)->setRate(rate);
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    std::string nodeDom =
        CodeAnalysis::getNodeDomainName(node, scope, m_ctx.tree);
    for (const ASTNode &element : node->getChildren()) {
      double elementRate = CodeAnalysis::getNodeRate(element, scope, tree);
      if (elementRate < 0.0) {
        std::string elemDom =
            CodeAnalysis::getNodeDomainName(element, scope, m_ctx.tree);
        if (nodeDom.empty() || elemDom.empty() || nodeDom == elemDom) {
          setNodeRate(element, rate, scope, tree);
        }
      }
    }
  }
}

void RateAndTypeResolver::setNodeType(const ASTNode &node, const ASTNode &type,
                                      const ScopeStack &scope,
                                      const ASTNode &tree) {
  if (!node || !type) {
    return;
  }
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    if (auto declaration = ASTQuery::findDeclarationForNode(node, scope, tree)) {
      setDeclarationType(declaration, type);
    }
  }
}

void RateAndTypeResolver::resolveDeclarationDataTypes() {
  std::set<const void *> visited;
  for (const auto &node : m_ctx.tree->getChildren()) {
    if (node && (node->getNodeType() == AST::Declaration ||
                 node->getNodeType() == AST::ArrayDeclaration)) {
      resolveDataTypeForDeclaration(
          std::static_pointer_cast<DeclarationNode>(node), visited);
    }
  }
}

void RateAndTypeResolver::resolveDataTypeForDeclaration(
    const std::shared_ptr<DeclarationNode> &decl,
    std::set<const void *> &visited) {
  if (!decl || !visited.insert(decl.get()).second) {
    return;
  }

  auto typeClass = getDataTypeClass(decl);
  auto typesNode = typeClass ? typeClass->getPropertyValue("types") : nullptr;
  auto typeNode = decl->getPropertyValue("type");
  if (typesNode && !typesNode->getChildren().empty() &&
      (!typeNode || typeNode->getNodeType() == AST::None)) {
    std::string defaultType =
        ASTQuery::getNodeName(typesNode->getChildren().front());
    decl->setPropertyValue(
        "type", std::make_shared<EntityNode>(defaultType, __FILE__, __LINE__));
  }

  for (const auto &property : decl->getProperties()) {
    ASTNode value = property->getValue();
    if (!value) {
      continue;
    }
    if (value->getNodeType() == AST::List) {
      for (const auto &child : value->getChildren()) {
        if (child && (child->getNodeType() == AST::Declaration ||
                      child->getNodeType() == AST::ArrayDeclaration)) {
          resolveDataTypeForDeclaration(
              std::static_pointer_cast<DeclarationNode>(child), visited);
        }
      }
    } else if (value->getNodeType() == AST::Declaration ||
               value->getNodeType() == AST::ArrayDeclaration) {
      resolveDataTypeForDeclaration(
          std::static_pointer_cast<DeclarationNode>(value), visited);
    }
  }
}

std::shared_ptr<DeclarationNode> RateAndTypeResolver::getDataTypeClass(
    const std::shared_ptr<DeclarationNode> &decl) {
  std::string frameworkName;
  auto frameworkNode = decl->getCompilerProperty("framework");
  if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
    frameworkName =
        std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
  }
  for (const auto &className :
       ASTQuery::getDataTypeClassNames(decl, m_ctx.tree)) {
    if (auto typeClass = findTypeClass(className, frameworkName)) {
      return typeClass;
    }
  }
  return nullptr;
}

std::shared_ptr<DeclarationNode>
RateAndTypeResolver::findTypeClass(const std::string &name,
                                   const std::string &framework) {
  auto hasFramework = [](const std::shared_ptr<DeclarationNode> &decl) {
    auto frameworkNode = decl->getCompilerProperty("framework");
    return frameworkNode && frameworkNode->getNodeType() == AST::String &&
           !std::static_pointer_cast<ValueNode>(frameworkNode)
                ->getStringValue()
                .empty();
  };
  auto typeClass = ASTQuery::findTypeClass(name, m_ctx.tree, framework);
  if (typeClass && hasFramework(typeClass)) {
    return typeClass;
  }
  if (!m_ctx.system) {
    return typeClass;
  }
  auto objects = m_ctx.system->getImportTrees();
  for (const auto &objectsPair : objects) {
    for (const auto &node : objectsPair.second) {
      if (!node || node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "typeClass" && decl->getName() == name &&
          hasFramework(decl)) {
        m_ctx.tree->addChild(decl);
        ASTFunctions::insertRequiredObjectsForNode(decl, objects, m_ctx.tree);
        return ASTQuery::findTypeClass(name, m_ctx.tree, framework);
      }
    }
  }
  return typeClass;
}

void RateAndTypeResolver::setDeclarationType(
    const std::shared_ptr<DeclarationNode> &decl, const ASTNode &type) {
  auto oldType = decl->getPropertyValue("type");
  if (oldType && oldType->getNodeType() != AST::None) {
    return;
  }
  if (ASTQuery::getDataTypeClassNames(decl, m_ctx.tree).empty()) {
    return;
  }
  auto typeClass = getDataTypeClass(decl);
  if (typeClass && type->getNodeType() == AST::Entity &&
      !ASTQuery::isTypeClassMember(typeClass, ASTQuery::getNodeName(type)) &&
      decl->getCompilerProperty("autoDeclared")) {
    static const std::map<std::string, std::string> builtinTypeDeclarations = {
        {DataTypes::Switch, "switch"}, {DataTypes::String, "string"}};
    auto builtin = builtinTypeDeclarations.find(ASTQuery::getNodeName(type));
    if (builtin != builtinTypeDeclarations.end()) {
      changeDeclarationType(decl, builtin->second);
      return;
    }
  }
  decl->setPropertyValue("type", type);
}

void RateAndTypeResolver::changeDeclarationType(
    const std::shared_ptr<DeclarationNode> &decl,
    const std::string &entityType) {
  decl->setEntityType(entityType);
  auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_ctx.tree);
  if (!typeDecl && m_ctx.system) {
    auto objects = m_ctx.system->getImportTrees();
    ASTFunctions::insertRequiredObjectsForNode(decl, objects, m_ctx.tree);
    typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_ctx.tree);
  }
  std::vector<std::string> portNames;
  if (typeDecl) {
    for (const auto &port :
         ASTQuery::getPortsForTypeEntity(typeDecl, {}, m_ctx.tree)) {
      auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
      auto nameNode = portDecl->getPropertyValue("name");
      if (nameNode && nameNode->getNodeType() == AST::String) {
        portNames.push_back(
            std::static_pointer_cast<ValueNode>(nameNode)->getStringValue());
      }
    }
  }
  const auto properties = decl->getProperties();
  for (const auto &property : properties) {
    if (property->getName() == "default" ||
        (typeDecl && std::find(portNames.begin(), portNames.end(),
                               property->getName()) == portNames.end())) {
      decl->removeProperty(property);
    }
  }
  ASTFunctions::fillDefaultPropertiesForNode(decl, m_ctx.tree->getChildren());
}
