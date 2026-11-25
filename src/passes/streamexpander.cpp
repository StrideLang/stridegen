/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "streamexpander.hpp"

#include "astnormalizer.hpp"
#include "rateandtyperesolver.hpp"
#include "stride/parser/arraynode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/generatorrangenode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/rangenode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include <cassert>
#include <functional>
#include <iostream>

using namespace strd;

namespace {

ASTNode makeZeroOrFalseForArrayDecl(const std::shared_ptr<DeclarationNode> &decl,
                                    const ASTNode &locNode,
                                    const ScopeStack &scope,
                                    const ASTNode &tree) {
  const char *file = locNode->getFilename().c_str();
  int line = locNode->getLine();
  if (decl) {
    if (decl->getEntityType() == "switch" ||
        CodeAnalysis::getDataTypeForDeclaration(decl, scope, tree) ==
            DataTypes::Switch) {
      return std::make_shared<ValueNode>(false, file, line);
    }
    return std::make_shared<ValueNode>(static_cast<int64_t>(0), file, line);
  }
  return std::make_shared<ValueNode>(false, file, line);
}

ASTNode expandRangesInASTNode(const ASTNode &node, const ScopeStack &scope,
                              const ASTNode &tree) {
  if (!node) {
    return nullptr;
  }

  if (node->getNodeType() == AST::GeneratorRange) {
    auto genRange = std::static_pointer_cast<GeneratorRangeNode>(node);
    int64_t start = ASTFunctions::evaluateConstInteger(genRange->startIndex(),
                                                       scope, tree, nullptr);
    int64_t end = ASTFunctions::evaluateConstInteger(genRange->endIndex(),
                                                     scope, tree, nullptr);
    int64_t step = 1;
    if (genRange->stepIndex()) {
      step = ASTFunctions::evaluateConstInteger(genRange->stepIndex(), scope,
                                                tree, nullptr);
    }
    auto expandedList = std::make_shared<ListNode>(
        nullptr, node->getFilename().c_str(), node->getLine());
    if (step > 0 && end >= start) {
      for (int64_t i = start; i <= end; i += step) {
        expandedList->addChild(std::make_shared<ValueNode>(
            i, node->getFilename().c_str(), node->getLine()));
      }
    } else if (step < 0 && end <= start) {
      for (int64_t i = start; i >= end; i += step) {
        expandedList->addChild(std::make_shared<ValueNode>(
            i, node->getFilename().c_str(), node->getLine()));
      }
    }
    return expandedList;
  }

  if (node->getNodeType() == AST::Stream) {
    auto s = std::static_pointer_cast<StreamNode>(node);
    s->setLeft(expandRangesInASTNode(s->getLeft(), scope, tree));
    s->setRight(expandRangesInASTNode(s->getRight(), scope, tree));
    return s;
  }

  if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    auto indexList = arrayNode->index();
    if (!indexList || indexList->getChildren().empty()) {
      return node;
    }

    auto resolveArrayElement =
        [&](const std::string &arrayName, const ASTNode &idxNode,
            const std::vector<std::string> &ns) -> ASTNode {
      std::vector<LangError> evalErrors;
      int64_t evaluatedIdx = ASTFunctions::evaluateConstInteger(
          idxNode, scope, tree, &evalErrors);
      if (evalErrors.empty()) {
        if (evaluatedIdx < 0) {
          auto decl =
              ASTQuery::findDeclarationByName(arrayName, scope, tree, ns);
          return makeZeroOrFalseForArrayDecl(decl, node, scope, tree);
        }
        auto singleIdxList = std::make_shared<ListNode>(
            std::make_shared<ValueNode>(
                evaluatedIdx, node->getFilename().c_str(), node->getLine()),
            node->getFilename().c_str(), node->getLine());
        return std::make_shared<ArrayNode>(arrayName, singleIdxList,
                                           node->getFilename().c_str(),
                                           node->getLine(), ns);
      }
      auto singleIdxList = std::make_shared<ListNode>(
          idxNode->deepCopy(), node->getFilename().c_str(), node->getLine());
      return std::make_shared<ArrayNode>(arrayName, singleIdxList,
                                         node->getFilename().c_str(),
                                         node->getLine(), ns);
    };

    bool hasCollection = false;
    std::vector<ASTNode> expandedIndexItems;
    for (const auto &idx : indexList->getChildren()) {
      if (idx && idx->getNodeType() == AST::Range) {
        hasCollection = true;
        auto range = std::static_pointer_cast<RangeNode>(idx);
        int64_t start = ASTFunctions::evaluateConstInteger(
            range->startIndex(), scope, tree, nullptr);
        int64_t end = ASTFunctions::evaluateConstInteger(range->endIndex(),
                                                         scope, tree, nullptr);
        if (end >= start) {
          for (int64_t i = start; i <= end; ++i) {
            expandedIndexItems.push_back(std::make_shared<ValueNode>(
                i, node->getFilename().c_str(), node->getLine()));
          }
        }
      } else {
        auto expIdx = expandRangesInASTNode(idx, scope, tree);
        if (expIdx && expIdx->getNodeType() == AST::List) {
          hasCollection = true;
          for (const auto &sub : expIdx->getChildren()) {
            expandedIndexItems.push_back(sub);
          }
        } else {
          expandedIndexItems.push_back(expIdx ? expIdx : idx);
        }
      }
    }

    if (hasCollection) {
      auto expandedList = std::make_shared<ListNode>(
          nullptr, node->getFilename().c_str(), node->getLine());
      for (const auto &idxItem : expandedIndexItems) {
        expandedList->addChild(resolveArrayElement(
            arrayNode->getName(), idxItem, arrayNode->getNamespaceList()));
      }
      return expandedList;
    }
    if (expandedIndexItems.size() == 1) {
      std::vector<LangError> evalErrors;
      int64_t evaluatedIdx = ASTFunctions::evaluateConstInteger(
          expandedIndexItems[0], scope, tree, &evalErrors);
      if (evalErrors.empty() && evaluatedIdx < 0) {
        auto decl = ASTQuery::findDeclarationForNode(arrayNode, scope, tree);
        return makeZeroOrFalseForArrayDecl(decl, node, scope, tree);
      }
    }
    return node;
  }

  if (node->getNodeType() == AST::List) {
    auto listNode = std::static_pointer_cast<ListNode>(node);
    auto newList = std::make_shared<ListNode>(
        nullptr, node->getFilename().c_str(), node->getLine());
    for (const auto &child : listNode->getChildren()) {
      auto expChild = expandRangesInASTNode(child, scope, tree);
      if (child && child->getNodeType() == AST::GeneratorRange && expChild &&
          expChild->getNodeType() == AST::List) {
        for (const auto &item : expChild->getChildren()) {
          newList->addChild(item);
        }
      } else {
        newList->addChild(expChild);
      }
    }
    return newList;
  }

  return node;
}

int64_t autoBroadcastCount(ASTNode &left,
                           const std::shared_ptr<FunctionNode> &funcNode,
                           const ScopeStack &scope, const ASTNode &tree) {
  if (!left || funcNode->getCompilerProperty("_bundleIndices")) {
    return 0;
  }
  auto funcDecl = ASTQuery::findDeclarationForNode(funcNode, scope, tree);
  if (!funcDecl) {
    return 0;
  }
  const int numInputs =
      CodeAnalysis::getTypeNumInputs(funcDecl, scope, tree, nullptr);
  if (numInputs < 1) {
    return 0;
  }

  const std::vector<ASTNode> items = left->getNodeType() == AST::List
                                         ? left->getChildren()
                                         : std::vector<ASTNode>{left};
  std::vector<ASTNode> expandedItems;
  std::vector<int64_t> widths;
  for (const auto &item : items) {
    if (item && item->getNodeType() == AST::List) {
      expandedItems.push_back(item);
      widths.push_back(static_cast<int64_t>(item->getChildren().size()));
    } else if (item && item->getNodeType() == AST::Entity) {
      const int size =
          CodeAnalysis::getNodeNumOutputs(item, scope, tree, nullptr);
      if (size < 0) {
        return 0;
      }
      if (size > 1) {
        auto entityNode = std::static_pointer_cast<EntityNode>(item);
        auto elements = std::make_shared<ListNode>(
            nullptr, item->getFilename().c_str(), item->getLine());
        for (int64_t i = 0; i < size; ++i) {
          auto indexList = std::make_shared<ListNode>(
              std::make_shared<ValueNode>(i, item->getFilename().c_str(),
                                          item->getLine()),
              item->getFilename().c_str(), item->getLine());
          elements->addChild(std::make_shared<ArrayNode>(
              entityNode->getName(), indexList, item->getFilename().c_str(),
              item->getLine(), entityNode->getNamespaceList()));
        }
        expandedItems.push_back(elements);
      } else {
        expandedItems.push_back(item);
      }
      widths.push_back(size > 1 ? size : 1);
    } else {
      expandedItems.push_back(item);
      widths.push_back(1);
    }
  }

  auto newLeft = std::make_shared<ListNode>(
      nullptr, left->getFilename().c_str(), left->getLine());
  int64_t count = 0;
  if (numInputs == 1) {
    for (const auto &item : expandedItems) {
      if (item && item->getNodeType() == AST::List) {
        for (const auto &element : item->getChildren()) {
          newLeft->addChild(element);
        }
      } else {
        newLeft->addChild(item);
      }
    }
    count = static_cast<int64_t>(newLeft->getChildren().size());
  } else {
    if (static_cast<int>(items.size()) != numInputs) {
      return 0;
    }
    for (int64_t width : widths) {
      if (width > 1) {
        if (count > 1 && width != count) {
          auto widthsNode = std::make_shared<ListNode>(
              std::make_shared<ValueNode>(
                  count, funcNode->getFilename().c_str(), funcNode->getLine()),
              funcNode->getFilename().c_str(), funcNode->getLine());
          widthsNode->addChild(std::make_shared<ValueNode>(
              width, funcNode->getFilename().c_str(), funcNode->getLine()));
          funcNode->setCompilerProperty("_broadcastMismatch", widthsNode);
          return 0;
        }
        count = width;
      }
    }
    for (const auto &item : expandedItems) {
      newLeft->addChild(item);
    }
  }
  if (count <= 1) {
    return 0;
  }
  left = newLeft;
  return count;
}

ASTNode flattenListIfNested(const ASTNode &left) {
  if (!left || left->getNodeType() != AST::List) {
    return left;
  }
  auto inList = std::static_pointer_cast<ListNode>(left);
  bool hasNestedList = false;
  for (const auto &c : inList->getChildren()) {
    if (c && c->getNodeType() == AST::List) {
      hasNestedList = true;
      break;
    }
  }
  if (!hasNestedList) {
    return left;
  }
  auto flatList = std::make_shared<ListNode>(
      nullptr, inList->getFilename().c_str(), inList->getLine());
  std::function<void(const ASTNode &)> flatten = [&](const ASTNode &item) {
    if (item && item->getNodeType() == AST::List) {
      for (const auto &child : item->getChildren()) {
        flatten(child);
      }
    } else if (item) {
      flatList->addChild(item);
    }
  };
  flatten(inList);
  return flatList;
}

} // namespace

void StreamExpander::expandValueRanges() {
  auto expandProperty = [&](const std::shared_ptr<DeclarationNode> &decl,
                            const std::string &propertyName) {
    auto value = decl->getPropertyValue(propertyName);
    if (!value || value->getNodeType() != AST::List) {
      return;
    }
    bool hasRange = false;
    for (const auto &item : value->getChildren()) {
      if (item && item->getNodeType() == AST::GeneratorRange) {
        hasRange = true;
        break;
      }
    }
    if (!hasRange) {
      return;
    }
    auto expanded = std::make_shared<ListNode>(
        nullptr, value->getFilename().c_str(), value->getLine());
    for (const auto &item : value->getChildren()) {
      if (item && item->getNodeType() == AST::GeneratorRange) {
        auto values = expandRangesInASTNode(item, {}, m_ctx.tree);
        for (const auto &element : values->getChildren()) {
          expanded->addChild(element);
        }
      } else {
        expanded->addChild(item);
      }
    }
    decl->replacePropertyValue(propertyName, expanded);
  };

  std::function<void(const ASTNode &)> processContainer =
      [&](const ASTNode &container) {
        if (!container) {
          return;
        }
        for (const auto &child : container->getChildren()) {
          if (!child || (child->getNodeType() != AST::Declaration &&
                         child->getNodeType() != AST::ArrayDeclaration)) {
            continue;
          }
          auto decl = std::static_pointer_cast<DeclarationNode>(child);
          expandProperty(decl, "default");
          if (decl->getEntityType() == "constant") {
            expandProperty(decl, "value");
          }
          for (const auto &p : decl->getProperties()) {
            if (p && p->getValue() && p->getValue()->getNodeType() == AST::List) {
              processContainer(p->getValue());
            }
          }
        }
      };
  processContainer(m_ctx.tree);
}

std::vector<ASTNode> StreamExpander::expandSingleIndexedStream(
    const std::shared_ptr<StreamNode> &stream, const ScopeStack &scope,
    const ASTNode &tree,
    std::vector<std::pair<std::string, int64_t>> &newBundles) {
  if (!stream) {
    return {};
  }

  ASTNode left = expandRangesInASTNode(stream->getLeft(), scope, tree);
  ASTNode right = stream->getRight();

  std::shared_ptr<FunctionNode> funcNode;
  ASTNode outNode;

  if (right) {
    if (right->getNodeType() == AST::Stream) {
      auto subStream = std::static_pointer_cast<StreamNode>(right);
      if (subStream->getLeft() &&
          subStream->getLeft()->getNodeType() == AST::Function) {
        funcNode = std::static_pointer_cast<FunctionNode>(subStream->getLeft());
        outNode = expandRangesInASTNode(subStream->getRight(), scope, tree);
      }
    } else if (right->getNodeType() == AST::Function) {
      funcNode = std::static_pointer_cast<FunctionNode>(right);
    }
  }

  if (!funcNode) {
    stream->setLeft(left);
    if (right && right->getNodeType() == AST::Stream) {
      auto subStream = std::static_pointer_cast<StreamNode>(right);
      subStream->setRight(
          expandRangesInASTNode(subStream->getRight(), scope, tree));
    } else if (right) {
      stream->setRight(expandRangesInASTNode(right, scope, tree));
    }
    return {};
  }

  auto multiInstProp = funcNode->getCompilerProperty("_multiInstance");
  if (!multiInstProp) {
    multiInstProp = funcNode->getCompilerProperty("_arrayIndices");
  }
  if (!multiInstProp && autoBroadcastCount(left, funcNode, scope, tree) > 0) {
    multiInstProp = std::make_shared<ValueNode>(
        true, funcNode->getFilename().c_str(), funcNode->getLine());
  }
  if (!multiInstProp) {
    left = flattenListIfNested(left);
    stream->setLeft(left);
    if (outNode) {
      auto subStream = std::static_pointer_cast<StreamNode>(right);
      subStream->setRight(outNode);
    }
    return {};
  }

  ASTNode tailHead;
  ASTNode tailRest;
  if (outNode && outNode->getNodeType() == AST::Stream) {
    auto tail = std::static_pointer_cast<StreamNode>(outNode);
    tailHead = tail->getLeft();
    tailRest = tail->getRight();
  } else if (outNode && outNode->getNodeType() == AST::Function) {
    tailHead = outNode;
  }
  const bool outputsToArrays =
      tailHead && tailHead->getNodeType() == AST::Function;
  if (tailHead) {
    outNode = outputsToArrays ? nullptr
                              : expandRangesInASTNode(tailHead, scope, tree);
  }

  int64_t N = 0;
  if (left && left->getNodeType() == AST::List) {
    auto inList = std::static_pointer_cast<ListNode>(left);
    for (const auto &item : inList->getChildren()) {
      if (item && item->getNodeType() == AST::List) {
        int s = static_cast<int>(item->getChildren().size());
        if (s > N) {
          N = s;
        }
      }
    }
    if (N <= 0) {
      auto funcDecl = ASTQuery::findDeclarationForNode(funcNode, scope, tree);
      int numDeclInputs =
          funcDecl
              ? CodeAnalysis::getTypeNumInputs(funcDecl, scope, tree, nullptr)
              : 1;
      if (numDeclInputs >= 0 && numDeclInputs <= 1) {
        N = static_cast<int64_t>(inList->getChildren().size());
      }
    }
  } else if (left && left->getNodeType() == AST::Array) {
    int s = CodeAnalysis::getNodeNumOutputs(left, scope, tree, nullptr);
    if (s > N) {
      N = s;
    }
  }

  if (N <= 0 && outNode && outNode->getNodeType() == AST::List) {
    auto outList = std::static_pointer_cast<ListNode>(outNode);
    if (outList->getChildren().size() == 1 &&
        outList->getChildren()[0]->getNodeType() == AST::List) {
      N = static_cast<int64_t>(outList->getChildren()[0]->getChildren().size());
    } else {
      N = static_cast<int64_t>(outList->getChildren().size());
    }
  }
  if (N <= 0) {
    N = 1;
  }

  std::vector<ASTNode> instanceInputs(N);
  if (left && left->getNodeType() == AST::List) {
    auto inList = std::static_pointer_cast<ListNode>(left);
    const auto &inChildren = inList->getChildren();
    bool isFlatList = false;
    if (static_cast<int64_t>(inChildren.size()) == N) {
      bool allScalars = true;
      for (const auto &c : inChildren) {
        if (c && c->getNodeType() == AST::List &&
            static_cast<int64_t>(c->getChildren().size()) == N) {
          allScalars = false;
          break;
        }
      }
      if (allScalars) {
        auto funcDecl = ASTQuery::findDeclarationForNode(funcNode, scope, tree);
        int numDeclInputs =
            funcDecl
                ? CodeAnalysis::getTypeNumInputs(funcDecl, scope, tree, nullptr)
                : 1;
        if (numDeclInputs >= 0 && numDeclInputs <= 1) {
          isFlatList = true;
        }
      }
    }
    if (isFlatList) {
      for (int64_t i = 0; i < N; ++i) {
        instanceInputs[i] = inChildren[i]->deepCopy();
      }
    } else {
      for (int64_t i = 0; i < N; ++i) {
        auto tupleList = std::make_shared<ListNode>(
            nullptr, stream->getFilename().c_str(), stream->getLine());
        for (const auto &c : inChildren) {
          if (c && c->getNodeType() == AST::List &&
              static_cast<int64_t>(c->getChildren().size()) == N) {
            tupleList->addChild(c->getChildren()[i]->deepCopy());
          } else {
            tupleList->addChild(c->deepCopy());
          }
        }
        instanceInputs[i] = tupleList;
      }
    }
  } else if (left) {
    for (int64_t i = 0; i < N; ++i) {
      instanceInputs[i] = left->deepCopy();
    }
  }

  std::vector<ASTNode> instanceOutputs(N);
  if (outNode) {
    if (outNode->getNodeType() == AST::List) {
      auto outList = std::static_pointer_cast<ListNode>(outNode);
      if (static_cast<int64_t>(outList->getChildren().size()) == N) {
        for (int64_t i = 0; i < N; ++i) {
          instanceOutputs[i] = outList->getChildren()[i]->deepCopy();
        }
      } else if (outList->getChildren().size() == 1 &&
                 outList->getChildren()[0]->getNodeType() == AST::List &&
                 static_cast<int64_t>(
                     outList->getChildren()[0]->getChildren().size()) == N) {
        for (int64_t i = 0; i < N; ++i) {
          instanceOutputs[i] =
              outList->getChildren()[0]->getChildren()[i]->deepCopy();
        }
      } else {
        for (int64_t i = 0; i < N; ++i) {
          instanceOutputs[i] = outNode->deepCopy();
        }
      }
    } else {
      for (int64_t i = 0; i < N; ++i) {
        instanceOutputs[i] = outNode->deepCopy();
      }
    }
  }

  int64_t currentGroupId = ++m_parallelGroupId;
  std::string originFuncName = funcNode->getName();

  std::shared_ptr<ListNode> bundleElements;
  if (outputsToArrays) {
    std::string bundleName =
        "__" + originFuncName + "_Out" + std::to_string(currentGroupId);
    newBundles.push_back({bundleName, N});
    bundleElements = std::make_shared<ListNode>(
        nullptr, stream->getFilename().c_str(), stream->getLine());
    for (int64_t i = 0; i < N; ++i) {
      ASTNode element;
      if (N == 1) {
        element = std::make_shared<EntityNode>(
            bundleName, stream->getFilename().c_str(), stream->getLine());
      } else {
        auto indexList = std::make_shared<ListNode>(
            std::make_shared<ValueNode>(i, stream->getFilename().c_str(),
                                        stream->getLine()),
            stream->getFilename().c_str(), stream->getLine());
        element = std::make_shared<ArrayNode>(bundleName, indexList,
                                              stream->getFilename().c_str(),
                                              stream->getLine());
      }
      instanceOutputs[i] = element;
      bundleElements->addChild(element->deepCopy());
    }
  }

  auto tagParallelNode = [&](const ASTNode &target, int64_t index) {
    const char *file = stream->getFilename().c_str();
    int line = stream->getLine();
    target->setCompilerProperty(
        "_parallelGroup",
        std::make_shared<ValueNode>(currentGroupId, file, line));
    target->setCompilerProperty("_instanceIndex",
                                std::make_shared<ValueNode>(index, file, line));
    target->setCompilerProperty("_instanceCount",
                                std::make_shared<ValueNode>(N, file, line));
    target->setCompilerProperty(
        "_unrolledFrom",
        std::make_shared<ValueNode>(originFuncName, file, line));
  };

  std::vector<ASTNode> expandedStreams;
  expandedStreams.reserve(N);
  for (int64_t i = 0; i < N; ++i) {
    auto funcClone =
        std::static_pointer_cast<FunctionNode>(funcNode->deepCopy());
    funcClone->removeCompilerProperty("_arrayIndices");
    funcClone->removeCompilerProperty("_multiInstance");
    tagParallelNode(funcClone, i);

    ASTNode singleStream;
    if (instanceOutputs[i]) {
      auto sub = std::make_shared<StreamNode>(funcClone, instanceOutputs[i],
                                              stream->getFilename().c_str(),
                                              stream->getLine());
      singleStream = std::make_shared<StreamNode>(instanceInputs[i], sub,
                                                  stream->getFilename().c_str(),
                                                  stream->getLine());
    } else {
      singleStream = std::make_shared<StreamNode>(instanceInputs[i], funcClone,
                                                  stream->getFilename().c_str(),
                                                  stream->getLine());
    }
    tagParallelNode(singleStream, i);
    expandedStreams.push_back(singleStream);
  }

  if (outputsToArrays || tailRest) {
    std::shared_ptr<StreamNode> continuation;
    if (outputsToArrays) {
      ASTNode rest = tailHead;
      if (tailRest) {
        rest = std::make_shared<StreamNode>(tailHead, tailRest,
                                            stream->getFilename().c_str(),
                                            stream->getLine());
      }
      continuation = std::make_shared<StreamNode>(bundleElements, rest,
                                                  stream->getFilename().c_str(),
                                                  stream->getLine());
    } else {
      continuation = std::make_shared<StreamNode>(
          tailHead, tailRest, stream->getFilename().c_str(), stream->getLine());
    }
    auto expandedContinuation =
        expandSingleIndexedStream(continuation, scope, tree, newBundles);
    if (expandedContinuation.empty()) {
      expandedStreams.push_back(continuation);
    } else {
      expandedStreams.insert(expandedStreams.end(),
                             expandedContinuation.begin(),
                             expandedContinuation.end());
    }
  }

  return expandedStreams;
}

void StreamExpander::expandIndexedStreams() {
  std::vector<ASTNode> rootDeclarations;
  auto declareBundles =
      [&](const std::vector<std::pair<std::string, int64_t>> &bundles,
          const std::shared_ptr<DeclarationNode> &owner) {
        for (const auto &[name, size] : bundles) {
          auto bundle = AstNormalizer::createSignalDeclaration(
              name, static_cast<int>(size), {}, m_ctx.tree);
          auto entities = owner ? owner->getPropertyValue("entities") : nullptr;
          if (entities && entities->getNodeType() == AST::List) {
            entities->addChild(bundle);
          } else {
            rootDeclarations.push_back(bundle);
          }
        }
      };

  std::function<void(const ASTNode &, const std::shared_ptr<DeclarationNode> &)>
      processContainer = [&](const ASTNode &container,
                             const std::shared_ptr<DeclarationNode> &owner) {
        if (!container) {
          return;
        }
        auto children = container->getChildren();
        std::vector<ASTNode> newChildren;
        bool modified = false;

        for (const auto &child : children) {
          if (child && child->getNodeType() == AST::Stream) {
            auto stream = std::static_pointer_cast<StreamNode>(child);
            std::vector<std::pair<std::string, int64_t>> newBundles;
            auto expanded =
                expandSingleIndexedStream(stream, {}, m_ctx.tree, newBundles);
            declareBundles(newBundles, owner);
            if (!expanded.empty()) {
              newChildren.insert(newChildren.end(), expanded.begin(),
                                 expanded.end());
              modified = true;
            } else {
              newChildren.push_back(child);
            }
          } else {
            newChildren.push_back(child);
            if (child && (child->getNodeType() == AST::Declaration ||
                          child->getNodeType() == AST::ArrayDeclaration)) {
              auto decl = std::static_pointer_cast<DeclarationNode>(child);
              for (const char *propName :
                   {"streams", "updateGuard", "onEntry", "onProcess", "onExit",
                    "onTransition"}) {
                auto streams = decl->getPropertyValue(propName);
                if (!streams) {
                  continue;
                }
                if (streams->getNodeType() == AST::List) {
                  processContainer(streams, decl);
                } else if (streams->getNodeType() == AST::Stream) {
                  auto stream = std::static_pointer_cast<StreamNode>(streams);
                  std::vector<std::pair<std::string, int64_t>> newBundles;
                  auto expanded = expandSingleIndexedStream(stream, {},
                                                            m_ctx.tree,
                                                            newBundles);
                  declareBundles(newBundles, decl);
                  if (!expanded.empty()) {
                    auto newStreamsList = std::make_shared<ListNode>(
                        nullptr, streams->getFilename().c_str(),
                        streams->getLine());
                    for (const auto &s : expanded) {
                      newStreamsList->addChild(s);
                    }
                    decl->replacePropertyValue(propName, newStreamsList);
                  }
                }
              }
              for (const auto &p : decl->getProperties()) {
                if (p && p->getValue() &&
                    p->getValue()->getNodeType() == AST::List) {
                  for (const auto &item : p->getValue()->getChildren()) {
                    if (item &&
                        (item->getNodeType() == AST::Declaration ||
                         item->getNodeType() == AST::ArrayDeclaration)) {
                      processContainer(p->getValue(), decl);
                      break;
                    }
                  }
                }
              }
            }
          }
        }
        if (modified) {
          container->setChildren(newChildren);
        }
      };

  processContainer(m_ctx.tree, nullptr);
  for (const auto &decl : rootDeclarations) {
    m_ctx.tree->addChild(decl);
  }
}

void StreamExpander::expandParallel() {
  // Copy: the loop adds declarations to the tree.
  const std::vector<ASTNode> children = m_ctx.tree->getChildren();
  for (const ASTNode &node : children) {
    if (node && node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      expandParallelStream(stream, ScopeStack(), m_ctx.tree);
      auto declarations =
          declareUnknownStreamSymbols(stream, nullptr, ScopeStack(), m_ctx.tree);
      for (const ASTNode &decl : declarations) {
        if (decl &&
            !ASTQuery::findDeclarationForNode(decl, ScopeStack(), m_ctx.tree)) {
          m_ctx.tree->addChild(decl);
        }
      }
    }
  }
}

void StreamExpander::expandParallelStream(
    const std::shared_ptr<StreamNode> &stream, const ScopeStack &scopeStack,
    const ASTNode &tree) {
  std::shared_ptr<StreamNode> subStream = stream;
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  std::vector<std::pair<int, int>> IOs;

  while (right) {
    if (left->getNodeType() == AST::Function) {
      int funcInputs =
          CodeAnalysis::getNodeNumInputs(left, scopeStack, tree);
      if (funcInputs != CodeAnalysis::SIZE_MEMBER_ACCESS) {
        ASTNode newFunctions = expandFunctionFromProperties(
            std::static_pointer_cast<FunctionNode>(left), scopeStack, tree);
        if (newFunctions) {
          subStream->setLeft(newFunctions);
          left = subStream->getLeft();
        }
      }
    }
    IOs.push_back({CodeAnalysis::getNodeNumInputs(left, scopeStack, tree),
                   CodeAnalysis::getNodeNumOutputs(left, scopeStack, tree)});

    if (right->getNodeType() == AST::Stream) {
      subStream = std::static_pointer_cast<StreamNode>(right);
      left = subStream->getLeft();
      right = subStream->getRight();
    } else {
      if (right->getNodeType() == AST::Function) {
        ASTNode newFunctions = expandFunctionFromProperties(
            std::static_pointer_cast<FunctionNode>(right), scopeStack, tree);
        if (newFunctions) {
          subStream->setRight(newFunctions);
          right = subStream->getRight();
        }
      }
      IOs.push_back({CodeAnalysis::getNodeNumInputs(right, scopeStack, tree),
                     CodeAnalysis::getNodeNumOutputs(right, scopeStack, tree)});
      right = nullptr;
    }
  }

  std::vector<int> numCopies;
  numCopies.push_back(1);
  for (size_t i = 1; i < IOs.size(); ++i) {
    int numPrevOut = IOs[i - 1].second * numCopies.back();
    int numCurIn = IOs[i].first;
    if (numPrevOut == -1) {
      numCopies.push_back(1);
      continue;
    }
    if (numPrevOut > numCurIn) {
      if (numCurIn > 0 && numPrevOut % numCurIn == 0) {
        numCopies.push_back(numPrevOut / numCurIn);
      } else {
        numCopies.push_back(1);
      }
    } else {
      numCopies.push_back(1);
    }
  }
  if (numCopies.size() == IOs.size()) {
    expandStreamToSizes(stream, numCopies, -1, scopeStack);
  }
}

void StreamExpander::expandStreamToSizes(
    const std::shared_ptr<StreamNode> &stream, std::vector<int> &neededCopies,
    int previousOutSize, const ScopeStack &scopeStack) {
  ASTNode left = stream->getLeft();
  if (previousOutSize == -1) {
    previousOutSize = 1;
  }

  if (left->getNodeType() == AST::Function) {
    int numCopies = neededCopies.front();
    if (numCopies > 1) {
      auto newLeft = std::make_shared<ListNode>(
          left, left->getFilename().data(), left->getLine());
      for (int i = 1; i < numCopies; ++i) {
        newLeft->addChild(left->deepCopy());
      }
      stream->setLeft(newLeft);
    }
  }
  previousOutSize = CodeAnalysis::getNodeNumOutputs(stream->getLeft(),
                                                    scopeStack, m_ctx.tree);
  if (previousOutSize < 0) {
    previousOutSize = 1;
  }
  neededCopies.erase(neededCopies.begin());
  ASTNode right = stream->getRight();
  if (right->getNodeType() == AST::Stream) {
    expandStreamToSizes(std::static_pointer_cast<StreamNode>(right),
                        neededCopies, previousOutSize, scopeStack);
  } else {
    int rightSize = ASTQuery::getNodeSize(right, scopeStack, m_ctx.tree);
    if (right->getNodeType() == AST::Entity ||
        right->getNodeType() == AST::Function) {
      int numCopies = neededCopies.front();
      if (rightSize < 0 && right->getNodeType() == AST::Entity) {
        auto newDecls = declareUnknownEntity(
            std::static_pointer_cast<EntityNode>(right), previousOutSize,
            scopeStack, m_ctx.tree);
        for (const ASTNode &decl : newDecls) {
          m_ctx.tree->addChild(decl);
        }
      } else if (numCopies > 1 && right->getNodeType() == AST::Function) {
        auto newRight = std::make_shared<ListNode>(
            right, right->getFilename().data(), right->getLine());
        for (int i = 1; i < numCopies; ++i) {
          newRight->addChild(right->deepCopy());
        }
        stream->setRight(newRight);
      }
    }
    neededCopies.erase(neededCopies.begin());
    assert(neededCopies.empty());
  }
}

ASTNode StreamExpander::expandFunctionFromProperties(
    const std::shared_ptr<FunctionNode> &func, const ScopeStack &scopeStack,
    const ASTNode &tree) {
  int dataSize = CodeAnalysis::getFunctionNumInstances(func, scopeStack, tree);
  if (dataSize <= 1) {
    return nullptr;
  }

  auto props = func->getProperties();
  auto newFunctions = std::make_shared<ListNode>(
      nullptr, func->getFilename().c_str(), func->getLine());
  for (int i = 0; i < dataSize; ++i) {
    newFunctions->addChild(func->deepCopy());
  }

  for (const auto &prop : props) {
    ASTNode value = prop->getValue();
    int numOuts = CodeAnalysis::getNodeNumOutputs(value, scopeStack, tree);
    if (numOuts != 1 && numOuts != dataSize) {
      return nullptr;
    }
    if (numOuts == 1) {
      for (const ASTNode &newFunction : newFunctions->getChildren()) {
        std::static_pointer_cast<FunctionNode>(newFunction)
            ->replacePropertyValue(prop->getName(), value->deepCopy());
      }
    } else if (value->getNodeType() == AST::Entity) {
      auto nameNode = std::static_pointer_cast<EntityNode>(value);
      auto decl = ASTQuery::findDeclarationForNode(nameNode, scopeStack, tree);
      int size = ASTQuery::getEntityDeclaredSize(decl, scopeStack, tree);
      if (size != dataSize) {
        return nullptr;
      }
      for (int i = 0; i < size; ++i) {
        auto indexList = std::make_shared<ListNode>(
            std::make_shared<ValueNode>(static_cast<int64_t>(i),
                                        prop->getFilename().c_str(),
                                        prop->getLine()),
            prop->getFilename().c_str(), prop->getLine());
        auto newArrayNode = std::make_shared<ArrayNode>(
            nameNode->getName(), indexList, prop->getFilename().c_str(),
            prop->getLine(), nameNode->getNamespaceList());
        std::static_pointer_cast<FunctionNode>(newFunctions->getChildren()[i])
            ->replacePropertyValue(prop->getName(), newArrayNode);
      }
    } else if (value->getNodeType() == AST::List) {
      const auto &values = value->getChildren();
      const auto &functions = newFunctions->getChildren();
      if (values.size() != functions.size()) {
        return nullptr;
      }
      for (size_t i = 0; i < static_cast<size_t>(dataSize); ++i) {
        std::static_pointer_cast<FunctionNode>(functions[i])
            ->setPropertyValue(prop->getName(), values[i]->deepCopy());
      }
    }
  }
  return newFunctions;
}

void StreamExpander::resolveStreamSymbols() {
  for (const ASTNode &node : m_ctx.tree->getChildren()) {
    if (!node) {
      continue;
    }
    if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      auto declarations = declareUnknownStreamSymbols(stream, nullptr,
                                                      ScopeStack(), m_ctx.tree);
      for (const ASTNode &decl : declarations) {
        if (decl &&
            !ASTQuery::findDeclarationForNode(decl, ScopeStack(), m_ctx.tree)) {
          m_ctx.tree->addChild(decl);
        }
      }
    } else if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      auto streamBlocks =
          ASTQuery::getContainedStreamEntities(decl, ScopeStack(), m_ctx.tree);
      if (streamBlocks.empty()) {
        continue;
      }
      ScopeStack scopeStack;
      ASTNode entities = decl->getPropertyValue("entities");
      if (entities && entities->getNodeType() == AST::List) {
        scopeStack.push_back({node, entities->getChildren()});
      }
      for (auto blockIt = streamBlocks.rbegin(); blockIt != streamBlocks.rend();
           ++blockIt) {
        for (auto sIt = blockIt->streams.rbegin();
             sIt != blockIt->streams.rend(); ++sIt) {
          auto declarations = declareUnknownStreamSymbols(
              *sIt, nullptr, scopeStack, m_ctx.tree);
          auto entityList =
              std::static_pointer_cast<ListNode>(decl->getPropertyValue("entities"));
          if (entityList && entityList->getNodeType() == AST::List) {
            for (const ASTNode &newDecl : declarations) {
              if (newDecl && !ASTQuery::findDeclarationForNode(
                                 newDecl, scopeStack, m_ctx.tree)) {
                entityList->addChild(newDecl);
              }
            }
            scopeStack.clear();
            scopeStack.push_back({node, entityList->getChildren()});
          }
        }
      }
    }
  }
}

std::vector<ASTNode> StreamExpander::declareUnknownEntity(
    const std::shared_ptr<EntityNode> &entityNode, int size,
    const ScopeStack &localScope, const ASTNode &tree) {
  std::vector<ASTNode> declarations;
  if (!entityNode || entityNode->getName().empty()) {
    return declarations;
  }
  auto decl = ASTQuery::findDeclarationByName(
      entityNode->getName(), localScope, tree, entityNode->getNamespaceList());
  if (!decl) {
    auto newSignal = AstNormalizer::createSignalDeclaration(
        entityNode->getName(), size, localScope, m_ctx.tree);
    double rate = CodeAnalysis::getNodeRate(newSignal, ScopeStack(), tree);
    RateAndTypeResolver(m_ctx).setNodeRate(entityNode, rate, localScope, tree);
    declarations.push_back(newSignal);
  }
  return declarations;
}

std::vector<ASTNode> StreamExpander::declareUnknownExpressionSymbols(
    const std::shared_ptr<ExpressionNode> &expr, int size,
    const ScopeStack &scopeStack, const ASTNode &tree) {
  std::vector<ASTNode> newDeclarations;
  if (!expr) {
    return newDeclarations;
  }
  auto visitOperand = [&](const ASTNode &operand) {
    if (!operand) {
      return;
    }
    if (operand->getNodeType() == AST::Entity) {
      auto decls = declareUnknownEntity(
          std::static_pointer_cast<EntityNode>(operand), size, scopeStack,
          tree);
      newDeclarations.insert(newDeclarations.end(), decls.begin(), decls.end());
    } else if (operand->getNodeType() == AST::Expression) {
      auto decls = declareUnknownExpressionSymbols(
          std::static_pointer_cast<ExpressionNode>(operand), size, scopeStack,
          tree);
      newDeclarations.insert(newDeclarations.end(), decls.begin(), decls.end());
    }
  };

  if (expr->isUnary()) {
    visitOperand(expr->getValue());
  } else {
    visitOperand(expr->getLeft());
    visitOperand(expr->getRight());
  }
  return newDeclarations;
}

std::vector<ASTNode> StreamExpander::declareUnknownFunctionSymbols(
    const std::shared_ptr<FunctionNode> &func, const ScopeStack &scopeStack,
    const ASTNode &tree) {
  std::vector<ASTNode> newDeclarations;
  if (!func) {
    return newDeclarations;
  }
  for (const auto &property : func->getProperties()) {
    ASTNode value = property->getValue();
    if (value && value->getNodeType() == AST::Entity) {
      auto declarations = declareUnknownEntity(
          std::static_pointer_cast<EntityNode>(value), 1, scopeStack, tree);
      for (const ASTNode &declaration : declarations) {
        tree->addChild(declaration);
      }
    }
  }
  return newDeclarations;
}

std::vector<ASTNode> StreamExpander::declareUnknownStreamSymbols(
    const std::shared_ptr<StreamNode> &stream,
    const ASTNode &previousStreamMember, const ScopeStack &localScope,
    const ASTNode &tree) {
  std::vector<ASTNode> newDeclarations;
  if (!stream) {
    return newDeclarations;
  }

  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  if (!left || !right) {
    return newDeclarations;
  }

  ASTNode nextStreamMember =
      right->getNodeType() != AST::Stream
          ? right
          : std::static_pointer_cast<StreamNode>(right)->getLeft();

  if (left->getNodeType() == AST::Entity) {
    int size = -1;
    if (previousStreamMember) {
      size = CodeAnalysis::getNodeNumOutputs(previousStreamMember, localScope,
                                             m_ctx.tree);
    }
    if (size <= 0 && previousStreamMember) {
      size = CodeAnalysis::getNodeNumInputs(nextStreamMember, localScope,
                                            m_ctx.tree);
    }
    if (size <= 0) {
      size = 1;
    }
    auto declarations = declareUnknownEntity(
        std::static_pointer_cast<EntityNode>(left), size, localScope, tree);
    newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                           declarations.end());
  } else if (left->getNodeType() == AST::Expression) {
    auto declarations = declareUnknownExpressionSymbols(
        std::static_pointer_cast<ExpressionNode>(left), 1, localScope, tree);
    newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                           declarations.end());
  } else if (left->getNodeType() == AST::Function) {
    auto declarations = declareUnknownFunctionSymbols(
        std::static_pointer_cast<FunctionNode>(left), localScope, tree);
    newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                           declarations.end());
  }

  if (right->getNodeType() == AST::Stream) {
    auto declarations = declareUnknownStreamSymbols(
        std::static_pointer_cast<StreamNode>(right), left, localScope, tree);
    newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                           declarations.end());
  } else if (right->getNodeType() == AST::Entity) {
    int size = 0;
    if (left->getNodeType() == AST::Function) {
      if (auto funcDecl =
              ASTQuery::findDeclarationForNode(left, localScope, tree)) {
        size = CodeAnalysis::getTypeNumOutputs(funcDecl, localScope, m_ctx.tree);
      }
    } else {
      size = CodeAnalysis::getNodeNumOutputs(left, localScope, m_ctx.tree);
    }
    if (size <= 0) {
      size = 1;
    }
    auto declarations = declareUnknownEntity(
        std::static_pointer_cast<EntityNode>(right), size, localScope, tree);
    newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                           declarations.end());
  } else if (right->getNodeType() == AST::Function) {
    auto declarations = declareUnknownFunctionSymbols(
        std::static_pointer_cast<FunctionNode>(right), localScope, tree);
    newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                           declarations.end());
  }

  return newDeclarations;
}
