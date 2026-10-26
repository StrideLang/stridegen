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

#include "stride/stridegen/codevalidator.hpp"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/utils/astquery.h"
#include "stride/utils/astruntime.h"
#include "stride/utils/astvalidation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>

using namespace strd;

CodeValidator::CodeValidator(ASTNode tree, Options options)
    : m_system(nullptr), m_tree(tree), m_options(options) {
  validateTree(tree);
}

CodeValidator::~CodeValidator() {}

void CodeValidator::validateTree(ASTNode tree) {
  m_tree = tree;
  if (tree) {
    validate();
  }
}

bool CodeValidator::isValid() { return m_errors.size() == 0; }

void CodeValidator::validatePlatform(ASTNode tree,
                                     std::vector<LangError> &errors) {
  std::vector<std::shared_ptr<SystemNode>> systems =
      ASTQuery::getSystemNodes(tree);
  if (systems.size() == 0) {
    std::cout << __FILE__ << ":" << __LINE__ << "No system definition"
              << std::endl;
    return;
  }
  std::shared_ptr<SystemNode> platformNode = systems.at(0);

  for (size_t i = 1; i < systems.size(); i++) {
    std::cerr << "Ignoring system: " << platformNode->platformName()
              << std::endl;
    LangError error;
    error.type = LangError::SystemRedefinition;
    error.errorTokens.push_back(platformNode->platformName());
    error.filename = platformNode->getFilename();
    error.lineNumber = platformNode->getLine();
    errors.push_back(error);
  }
}

std::vector<StreamNode *> CodeValidator::getStreamsAtLine(ASTNode tree,
                                                          int line) {
  std::vector<StreamNode *> streams;
  for (ASTNode node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      if (node->getLine() == line) {
        streams.push_back(static_cast<StreamNode *>(node.get()));
      }
    }
  }
  return streams;
}

std::vector<LangError> CodeValidator::getErrors() { return m_errors; }

std::vector<LangError> CodeValidator::getWarnings() { return m_warnings; }

namespace {

// Whether a literal value is kept exactly by data type to
bool literalFits(const ASTNode &node, const DataTypes::Info &to) {
  auto value = std::static_pointer_cast<ValueNode>(node);
  if (to.isFloat()) {
    return true; // Literals are written for the type they go to
  }
  if (!to.isInteger()) {
    return false;
  }
  double real = node->getNodeType() == AST::Int
                    ? static_cast<double>(value->getIntValue())
                    : value->getRealValue();
  if (node->getNodeType() == AST::Real && real != std::trunc(real)) {
    return false;
  }
  double minimum = to.isSigned ? -std::ldexp(1.0, to.bits - 1) : 0.0;
  double maximum = to.isSigned ? std::ldexp(1.0, to.bits - 1) - 1
                               : std::ldexp(1.0, to.bits) - 1;
  return real >= minimum && real <= maximum;
}

void addConversionWarning(const ASTNode &node, const std::string &from,
                          const std::string &to, const std::string &reason,
                          std::vector<LangError> &warnings) {
  LangError warning;
  warning.type = LangError::LossyConversion;
  warning.severity = LangError::Warning;
  warning.filename = node->getFilename();
  warning.lineNumber = node->getLine();
  warning.columnNumber = node->getColumn();
  warning.errorTokens = {from, to, reason};
  warnings.push_back(warning);
}

std::string stringProperty(const ASTNode &node, const std::string &name) {
  auto property = node->getCompilerProperty(name);
  if (property && property->getNodeType() == AST::String) {
    return std::static_pointer_cast<ValueNode>(property)->getStringValue();
  }
  return std::string();
}

std::string trimmed(std::string text) {
  auto notSpace = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
  text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(),
             text.end());
  return text;
}

// operandOfRiskyPromotion: node is an operand of an expression already
// reported for its promotion, which covers the operand's conversion
void findLossyConversionsInNode(const ASTNode &node, const ASTNode &tree,
                                std::vector<LangError> &warnings,
                                std::set<const void *> &visited,
                                bool operandOfRiskyPromotion = false) {
  if (!node || !visited.insert(node.get()).second) {
    return;
  }
  auto to = stringProperty(node, "typecast");
  auto from = stringProperty(node, "typecastFrom");
  if (!to.empty() && !from.empty() && !operandOfRiskyPromotion) {
    auto risk = DataTypes::conversionRisk(from, to, tree);
    bool isLiteral =
        node->getNodeType() == AST::Int || node->getNodeType() == AST::Real;
    if (!risk.empty() &&
        !(isLiteral && literalFits(node, DataTypes::resolve(to, tree)))) {
      addConversionWarning(node, from, to, risk, warnings);
    }
  }
  bool riskyPromotion = false;
  if (node->getNodeType() == AST::Expression) {
    auto risk = stringProperty(node, "promotionRisk");
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
      findLossyConversionsInNode(property->getValue(), tree, warnings,
                                 visited);
    }
  }
  for (const auto &child : node->getChildren()) {
    findLossyConversionsInNode(child, tree, warnings, visited, riskyPromotion);
  }
}

} // namespace

std::vector<LangError> CodeValidator::findLossyConversions(ASTNode tree) {
  std::vector<LangError> warnings;
  std::set<const void *> visited;
  findLossyConversionsInNode(tree, tree, warnings, visited);
  // The resolver can keep copies of a node. Report each conversion once.
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

std::vector<std::string> CodeValidator::getPlatformErrors() {
  return m_system->getErrors();
}

void CodeValidator::validate() {
  m_errors.clear();
  m_warnings.clear();
  if (m_tree) {
    validatePlatform(m_tree, m_errors);
    ASTValidation::validateTypes(m_tree, m_errors, {}, m_tree, {}, "");
    ASTValidation::validateShadowing(m_tree, m_errors);
    ASTValidation::validateDataTypes(m_tree, m_errors);
    m_warnings = findLossyConversions(m_tree);

    validateArrayIndices(m_tree, m_errors, {});
    validateArraySizes(m_tree, m_errors, {});
    validateSymbolUniqueness({{nullptr, m_tree->getChildren()}}, m_errors);
    validateStreamSizes(m_tree, m_errors, {});
    validateRates(m_tree);
    validateConstraints(m_tree);
  }
  sortErrors();
}

void CodeValidator::validateArrayIndices(ASTNode node,
                                         std::vector<LangError> &errors,
                                         ScopeStack scope) {
  if (node->getNodeType() == AST::Array) {
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    auto index = arrayNode->index();
    if (index->getNodeType() != AST::Int &&
        index->getNodeType() != AST::MemberAccess &&
        index->getNodeType() != AST::Range &&
        index->getNodeType() != AST::List) {
      LangError error;
      error.type = LangError::InvalidIndexType;
      error.lineNumber = arrayNode->getLine();
      error.errorTokens.push_back(arrayNode->getName());
      errors.push_back(error);
    }
  }
  for (const ASTNode &child : node->getChildren()) {
    validateArrayIndices(child, errors, scope);
  }
}

void CodeValidator::validateArraySizes(ASTNode node,
                                       std::vector<LangError> &errors,
                                       ScopeStack scope) {
  if (node->getNodeType() == AST::ArrayDeclaration) {
    std::shared_ptr<DeclarationNode> declaration =
        std::static_pointer_cast<DeclarationNode>(node);
    // FIXME this needs to be rewritten looking at the port entity size
    int size =
        ASTQuery::getEntityDeclaredSize(declaration, scope, m_tree, &errors);
    int datasize = getEntityDataSize(declaration, scope, &errors);
    if (size != datasize && datasize > 1) {
      LangError error;
      error.type = LangError::ArraySizeMismatch;
      error.lineNumber = node->getLine();
      error.errorTokens.push_back(declaration->getArrayIndex()->getName());
      error.errorTokens.push_back(std::to_string(size));
      error.errorTokens.push_back(std::to_string(datasize));
      errors.push_back(error);
    }

    // TODO : use this pass to store the computed value of constant int?
    //    auto decl = static_pointer_cast<DeclarationNode>(node);
    //    auto subScope = getBlockSubScope(decl);
    //    if (subScope) {
    //      scope.push_back({decl->getName(), subScope->getChildren()});
    //    }
  }

  std::vector<ASTNode> children = node->getChildren();
  for (const ASTNode &node : children) {
    validateArraySizes(node, errors, scope);
  }
}

void CodeValidator::validateSymbolUniqueness(ScopeStack scope,
                                             std::vector<LangError> &errors) {
  const auto &scopeNodes = scope.back().second;
  std::unordered_map<std::string, std::vector<ASTNode>> declsByName;

  for (const auto &node : scopeNodes) {
    if (node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      std::string name = static_cast<DeclarationNode *>(node.get())->getName();
      if (!name.empty()) {
        declsByName[name].push_back(node);
      }
    }
  }

  for (const auto &[name, candidates] : declsByName) {
    if (candidates.size() <= 1) {
      continue;
    }
    for (size_t i = 0; i < candidates.size(); ++i) {
      const auto &node = candidates[i];
      for (size_t j = i + 1; j < candidates.size(); ++j) {
        const auto &sibling = candidates[j];
        // Check if framework matches. Library declarations have no
        // framework, so framework declarations can overlay them (e.g. the
        // dummy Real in the library).
        auto nodeFrameworkNode = node->getCompilerProperty("framework");
        auto siblingFrameworkNode = sibling->getCompilerProperty("framework");
        std::string nodeFramework;
        std::string siblingFramework;
        if (nodeFrameworkNode &&
            nodeFrameworkNode->getNodeType() == AST::String) {
          nodeFramework = std::static_pointer_cast<ValueNode>(nodeFrameworkNode)
                              ->getStringValue();
        }
        if (siblingFrameworkNode &&
            siblingFrameworkNode->getNodeType() == AST::String) {
          siblingFramework =
              std::static_pointer_cast<ValueNode>(siblingFrameworkNode)
                  ->getStringValue();
        }
        if (nodeFramework != siblingFramework) {
          continue;
        }

        // Check if scope matches
        auto nodeScopes = node->getNamespaceList();
        auto siblingScopes = sibling->getNamespaceList();
        if (nodeScopes.size() == siblingScopes.size()) {
          bool duplicateSymbol = true;
          for (size_t k = 0; k < nodeScopes.size(); k++) {
            if (nodeScopes[k] != siblingScopes[k]) {
              duplicateSymbol = false;
              break;
            }
          }
          if (!duplicateSymbol) {
            continue;
          }
          // Check if _at matches
          auto nodeAt = node->getCompilerProperty("_at");
          auto siblingAt = sibling->getCompilerProperty("_at");
          bool atMatches = false;
          if (!nodeAt && !siblingAt) {
            atMatches = true;
          }
          if (nodeAt && siblingAt) {
            if (nodeAt->getNodeType() == AST::Int &&
                siblingAt->getNodeType() == AST::Int) {
              auto nodeAtInt =
                  std::static_pointer_cast<ValueNode>(nodeAt)->getIntValue();
              auto siblingAtInt =
                  std::static_pointer_cast<ValueNode>(siblingAt)->getIntValue();
              if (nodeAtInt == siblingAtInt) {
                atMatches = true;
              }
            } else if (nodeAt->getNodeType() == AST::String &&
                       siblingAt->getNodeType() == AST::String) {
              auto nodeAtString =
                  std::static_pointer_cast<ValueNode>(nodeAt)->getStringValue();
              auto siblingAtString =
                  std::static_pointer_cast<ValueNode>(siblingAt)
                      ->getStringValue();
              if (nodeAtString == siblingAtString) {
                atMatches = true;
              }
            }
          }
          if (atMatches) {
            LangError error;
            error.type = LangError::DuplicateSymbol;
            error.lineNumber = sibling->getLine();
            error.filename = sibling->getFilename();
            error.errorTokens.push_back(name);
            error.errorTokens.push_back(node->getFilename());
            error.errorTokens.push_back(std::to_string(node->getLine()));
            errors.push_back(error);
          }
        }
      }
    }
  }

  // Recurse into code generator declarations
  for (const auto &node : scopeNodes) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (ASTQuery::isCodeGenerator(decl, scope, m_tree)) {
        if (decl->getPropertyValue("blocks") &&
            decl->getPropertyValue("ports")) {
          auto blocks = decl->getPropertyValue("blocks")->getChildren();
          auto ports = decl->getPropertyValue("ports")->getChildren();
          std::vector<ASTNode> subScope = blocks;
          subScope.insert(subScope.end(), ports.begin(), ports.end());
          scope.push_back({node, subScope});
          validateSymbolUniqueness(scope, errors);
          scope.pop_back();
        }
      }
    }
  }
}

void CodeValidator::validateStreamSizes(ASTNode tree,
                                        std::vector<LangError> &errors,
                                        ScopeStack scope) {
  for (ASTNode node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      StreamNode *stream = static_cast<StreamNode *>(node.get());
      validateStreamInputSize(stream, errors, scope);
    } else if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl) {
        if (decl->getEntityType() == "module" ||
            decl->getEntityType() == "reaction" ||
            decl->getEntityType() == "loop") {
          auto subScope = scope;
          subScope.push_back(
              {decl, decl->getPropertyValue("blocks")->getChildren()});
          auto streams = decl->getPropertyValue("streams")->getChildren();
          for (auto node : streams) {
            if (node->getNodeType() == AST::Stream) {
              auto stream = std::static_pointer_cast<StreamNode>(node);
              validateStreamInputSize(stream.get(), errors, subScope);
            }
          }

          // FIXME we need to validate streams recursively within modules and
          // reactions
        }
      }
    }
  }
}

void CodeValidator::validateRates(ASTNode tree) {
  if ((m_options & NO_RATE_VALIDATION) == 0) {
    for (ASTNode node : tree->getChildren()) {
      validateNodeRate(node, tree);
    }
  }
}

void CodeValidator::validateConstraints(ASTNode tree) {
  for (const ASTNode &node : tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      validateConstraints(std::static_pointer_cast<StreamNode>(node),
                          ScopeStack(), tree);
    }
  }
}

void CodeValidator::validateNodeRate(ASTNode node, ASTNode tree) {
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
      std::shared_ptr<DeclarationNode> domDecl = nullptr;
      if (domNode) {
        std::string domId = CodeAnalysis::getDomainIdentifier(domNode, {}, tree);
        if (!domId.empty()) {
          domDecl = CodeAnalysis::findDomainDeclaration(domId, tree);
        }
      }
      if (rateNode && rateNode->getNodeType() != AST::None) {
        double rate = CodeAnalysis::getNodeRate(decl, {}, tree);
        if (!isPropagated && rate <= 0) {
          LangError error;
          error.type = LangError::UnresolvedRate;
          error.lineNumber =
              rateNode->getLine() > 0 ? rateNode->getLine() : node->getLine();
          error.filename = node->getFilename();
          error.errorTokens.push_back(decl->getName());
          m_errors.push_back(error);
        } else if (!isPropagated && rate > 0 && domDecl) {
          double domRate = CodeAnalysis::getDomainDefaultRate(domDecl);
          if (domRate <= 0) {
            LangError warning;
            warning.type = LangError::IgnoredRateInOneShotDomain;
            warning.severity = LangError::Warning;
            warning.lineNumber =
                rateNode->getLine() > 0 ? rateNode->getLine() : node->getLine();
            warning.filename = node->getFilename();
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
      std::string domId = CodeAnalysis::getDomainIdentifier(domNode, {}, tree);
      auto domDecl =
          !domId.empty() ? CodeAnalysis::findDomainDeclaration(domId, tree) : nullptr;
      if (domDecl && CodeAnalysis::getDomainDefaultRate(domDecl) > 0) {
        auto decl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(node), {}, tree, node->getNamespaceList());
        if (decl && (decl->getEntityType() == "signal" ||
                     decl->getEntityType() == "switch")) {
          double rate = CodeAnalysis::getNodeRate(node, {}, tree);
          if (rate <= 0) {
            LangError error;
            error.type = LangError::UnresolvedRate;
            error.lineNumber = node->getLine();
            error.filename = node->getFilename();
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
  } else if (node->getNodeType() == AST::Expression) {
    for (const ASTNode &child : node->getChildren()) {
      validateNodeRate(child, tree);
    }
  } else if (node->getNodeType() == AST::Function) {
    for (const std::shared_ptr<PropertyNode> &prop :
         static_cast<FunctionNode *>(node.get())->getProperties()) {
      validateNodeRate(prop->getValue(), tree);
    }
  }
  // TODO also need to validate rates within module and reaction streams
}

void CodeValidator::validateConstraints(std::shared_ptr<StreamNode> stream,
                                        ScopeStack scopeStack, ASTNode tree) {
  auto node = stream->getLeft();
  while (node) {
    if (node->getNodeType() == AST::Function) {
      validateFunctionConstraints(std::static_pointer_cast<FunctionNode>(node),
                                  scopeStack, tree);
    }

    if (stream == nullptr) {
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
    std::shared_ptr<FunctionNode> function, ScopeStack scopeStack,
    ASTNode tree) {
  // FIXME use framework when searching for function.
  auto declaration = ASTQuery::findDeclarationByName(
      function->getName(), scopeStack, tree, function->getNamespaceList());
  if (!declaration) {
    return;
  }
  auto constraintsNode = declaration->getPropertyValue("constraints");
  auto blocks = declaration->getPropertyValue("blocks");

  if (blocks) {
    scopeStack.push_back({nullptr, blocks->getChildren()});
  } else {
    scopeStack.push_back({nullptr, {}});
  }
  if (constraintsNode) {
    if (constraintsNode->getNodeType() == AST::Stream) {

      validateConstraintStream(
          std::static_pointer_cast<StreamNode>(constraintsNode), function,
          declaration, scopeStack, tree);
    } else if (constraintsNode->getNodeType() == AST::List) {
      for (const auto &constraint : constraintsNode->getChildren()) {
        if (constraint->getNodeType() == AST::Stream) {
          validateConstraintStream(
              std::static_pointer_cast<StreamNode>(constraint), function,
              declaration, scopeStack, tree);
        } else {
          std::cerr
              << "WARNING: Unexpected type in constraint, expecting stream"
              << std::endl;
        }
      }
    } else {
      std::cerr << __FILE__ << ":" << __LINE__
                << " constraints must be streams or list of streams"
                << std::endl;
    }
  }
}

void CodeValidator::validateConstraintStream(
    std::shared_ptr<StreamNode> stream, std::shared_ptr<FunctionNode> function,
    std::shared_ptr<DeclarationNode> declaration, ScopeStack scopeStack,
    ASTNode tree) {
  auto node = stream->getLeft();
  std::vector<ASTNode> previous;
  while (node) {
    previous = resolveConstraintNode(node, previous, function, declaration,
                                     scopeStack, tree);
    if (!stream) {
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
    ASTNode node, std::vector<ASTNode> previous,
    std::shared_ptr<FunctionNode> function,
    std::shared_ptr<DeclarationNode> declaration, ScopeStack scopeStack,
    ASTNode tree) {
  if (node->getNodeType() == AST::MemberAccess) {
    auto pp = std::static_pointer_cast<MemberAccessNode>(node);
    if (pp->getPropertyName() == "size") {
      return {std::make_shared<ValueNode>(
          (int64_t)CodeAnalysis::evaluateSizeMemberAccess(
              pp->getEntity(), scopeStack, declaration, function, tree),
          __FILE__, __LINE__)};
    }
    if (pp->getPropertyName() == "rate") {
      return {std::make_shared<ValueNode>(
          CodeAnalysis::evaluateRateMemberAccess(pp->getEntity(), scopeStack,
                                                 declaration, function, tree),
          __FILE__, __LINE__)};
    } else {
      return {std::make_shared<AST>()};
    }

  } else if (node->getNodeType() == AST::Function) {
    // FIXME the output blocks must be determined from the function.
    // Currently providing only a switch block for output as the only
    // implemented functions are
    StrideRuntimeStatus status;
    std::vector<ASTNode> output;
    output.emplace_back(std::make_shared<ValueNode>(true, __FILE__, __LINE__));
    auto functionInstance = std::static_pointer_cast<FunctionNode>(node);
    ASTRuntime::resolveFunction(functionInstance, previous, output, status);
    if (!status.ok) {
      auto &err = status.err;
      err.filename = functionInstance->getFilename();
      err.lineNumber = functionInstance->getLine();
      err.errorTokens[0] = functionInstance->getName();
      m_errors.push_back(err);
    }
    return output;
  } else if (node->getNodeType() == AST::Entity) {

  } else if (node->getNodeType() == AST::List) {
    std::vector<ASTNode> resolvedList;
    size_t i = 0;
    for (const auto &elem : node->getChildren()) {
      ASTNode previousNode;
      if (previous.size() > i) {
        previousNode = previous[i];
      }
      auto resolved = resolveConstraintNode(elem, {previousNode}, function,
                                            declaration, scopeStack, tree);
      resolvedList.insert(resolvedList.end(), resolved.begin(), resolved.end());
      i++;
    }
    return resolvedList;
  } else {
    return {node};
  }
  return std::vector<ASTNode>();
}

void CodeValidator::sortErrors() {
  std::sort(m_errors.begin(), m_errors.end(),
            [](const LangError &err1, const LangError &err2) {
              return err1.lineNumber < err2.lineNumber;
            });
}

void CodeValidator::validateStreamInputSize(StreamNode *stream,
                                            std::vector<LangError> &errors,
                                            ScopeStack scope) {
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();

  int leftOutSize =
      CodeAnalysis::getNodeNumOutputs(left, scope, m_tree, &errors);
  int rightInSize =
      CodeAnalysis::getNodeNumInputs(right, scope, m_tree, &errors);

  auto leftDecl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(left),
                                                  scope, m_tree);
  std::shared_ptr<DeclarationNode> rightDecl;
  if (right->getNodeType() == AST::Stream) {
    auto nextStreamMember =
        std::static_pointer_cast<StreamNode>(right)->getLeft();
    rightDecl = ASTQuery::findDeclarationByName(
        ASTQuery::getNodeName(nextStreamMember), scope, m_tree);
  } else {
    rightDecl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(right),
                                                scope, m_tree);
  }
  if ((leftDecl && leftDecl->getEntityType() == "buffer") ||
      (rightDecl && rightDecl->getEntityType() == "buffer")) {
    // FIXME how should buffer size be validated?

  } else if ((leftDecl && leftDecl->getEntityType() == "table") ||
             (rightDecl && rightDecl->getEntityType() == "table")) {
    // FIXME how should table size be validated?

  } else {
    if (leftOutSize == -2 || rightInSize == -2) {
      // FIXME Hack while calculation of port member access size is implemented
    } else {
      if ((leftOutSize != rightInSize &&
           ((int)(rightInSize / (double)leftOutSize)) !=
               (rightInSize / (double)leftOutSize)) ||
          rightInSize == 0) {
        LangError error;
        error.type = LangError::StreamMemberSizeMismatch;
        error.lineNumber = right->getLine();
        error.errorTokens.push_back(std::to_string(leftOutSize));
        error.errorTokens.push_back(ASTQuery::getNodeName(left));
        error.errorTokens.push_back(std::to_string(rightInSize));
        error.filename = left->getFilename();
        errors.push_back(error);
      }
    }
  }

  if (right->getNodeType() == AST::Stream) {
    validateStreamInputSize(static_cast<StreamNode *>(right.get()), errors,
                            scope);
  }
}

int CodeValidator::getEntityDataSize(
    std::shared_ptr<DeclarationNode> declaration, ScopeStack scope,
    std::vector<LangError> *errors) {
  std::vector<std::shared_ptr<PropertyNode>> ports =
      declaration->getProperties();
  if (ports.size() == 0) {
    return 0;
  }
  int size = CodeAnalysis::getNodeNumOutputs(ports.at(0)->getValue(), scope,
                                             m_tree, errors);
  for (std::shared_ptr<PropertyNode> port : ports) {
    ASTNode value = port->getValue();
    int newSize = CodeAnalysis::getNodeNumOutputs(value, scope, m_tree, errors);
    if (size != newSize) {
      if (size == 1 && newSize > 0) {
        size = newSize;
      }
      if (newSize == 1) {
        // just ignore
      } else {
        //        size = -1;
      }
    }
  }
  return size;
}
