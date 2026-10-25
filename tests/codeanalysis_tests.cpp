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

#include "gtest/gtest.h"

#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridegen/coderesolver.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/generatorrangenode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

using namespace strd;

TEST(CodeAnalysis, TypeTree) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "codeanalysis/typetree.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto typeTree = CodeAnalysis::getStateStructInformation(ScopeStack(), tree);

  ASSERT_EQ(typeTree.internal.size(), 4);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[0].first), "Input");
  EXPECT_EQ(typeTree.internal[0].second, "_IntType");
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[1].first), "Output");
  EXPECT_EQ(typeTree.internal[1].second, "_IntType");
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[2].first), "ListInput");
  EXPECT_EQ(typeTree.internal[2].second, "_IntType");
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[3].first), "ListOutput");
  EXPECT_EQ(typeTree.internal[3].second, "_IntType");

  ASSERT_EQ(typeTree.nodes.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].instance), "Mod");
  ASSERT_EQ(typeTree.nodes[0].input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].input[0].first),
            "ModInput");
  ASSERT_EQ(typeTree.nodes[0].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].output[0].first),
            "ModOutput");
  ASSERT_EQ(typeTree.nodes[0].internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].internal[0].first),
            "ModInternal");
}

TEST(CodeAnalysis, TypeTreeModuleNested) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR
                        "codeanalysis/typetree_module_nested.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto typeTree = CodeAnalysis::getStateStructInformation(ScopeStack(), tree);

  ASSERT_EQ(typeTree.internal.size(), 4);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[0].first), "NestedInput");
  EXPECT_EQ(typeTree.internal[0].second, "_IntType");
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[1].first), "NestedOutput");
  EXPECT_EQ(typeTree.internal[1].second, "_IntType");
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[2].first), "Input");
  EXPECT_EQ(typeTree.internal[2].second, "_IntType");
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[3].first), "Output");
  EXPECT_EQ(typeTree.internal[3].second, "_IntType");

  ASSERT_EQ(typeTree.nodes.size(), 4);

  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].instance), "NestingMod");
  ASSERT_EQ(typeTree.nodes[0].input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].input[0].first),
            "NestingModInput");
  ASSERT_EQ(typeTree.nodes[0].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].output[0].first),
            "NestingModOutput");
  ASSERT_EQ(typeTree.nodes[0].internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].internal[0].first),
            "NestingModInternal");
  ASSERT_EQ(typeTree.nodes[0].nodes.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].instance),
            "NestedMod");
  ASSERT_EQ(typeTree.nodes[0].nodes[0].input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].input[0].first),
            "NestedModInput");
  ASSERT_EQ(typeTree.nodes[0].nodes[0].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].output[0].first),
            "NestedModOutput");
  ASSERT_EQ(typeTree.nodes[0].nodes[0].internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].internal[0].first),
            "NestedModInternal");

  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[1].instance), "Mod");
  ASSERT_EQ(typeTree.nodes[1].input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[1].input[0].first),
            "ModInput");
  ASSERT_EQ(typeTree.nodes[1].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[1].output[0].first),
            "ModOutput");
  ASSERT_EQ(typeTree.nodes[1].internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[1].internal[0].first),
            "ModInternal");

  ASSERT_EQ(typeTree.nodes[1].nodes.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[1].nodes[0].instance),
            "NestedReaction");
  ASSERT_EQ(typeTree.nodes[1].nodes[0].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[1].nodes[0].output[0].first),
            "NestedReactionOutput");
  ASSERT_EQ(typeTree.nodes[1].nodes[0].internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[1].nodes[0].internal[0].first),
            "NestedReactionInternal");

  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].instance), "ModWithLoop");
  ASSERT_EQ(typeTree.nodes[2].input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].input[0].first),
            "ModInput");
  ASSERT_EQ(typeTree.nodes[2].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].output[0].first),
            "ModOutput");
  ASSERT_EQ(typeTree.nodes[2].internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].internal[0].first),
            "ModInternal");

  ASSERT_EQ(typeTree.nodes[2].nodes.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].nodes[0].instance),
            "NestedLoop");
  ASSERT_EQ(typeTree.nodes[2].nodes[0].input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].nodes[0].input[0].first),
            "Input");
  ASSERT_EQ(typeTree.nodes[2].nodes[0].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].nodes[0].output[0].first),
            "Output");
  ASSERT_EQ(typeTree.nodes[2].nodes[0].internal.size(), 2);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].nodes[0].internal[0].first),
            "Index");
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[2].nodes[0].internal[1].first),
            "Done");

  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[3].instance),
            "ModulePersistentVar");
  ASSERT_EQ(typeTree.nodes[3].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[3].output[0].first), "Output");
  ASSERT_EQ(typeTree.nodes[3].input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[3].input[0].first), "Input");
  ASSERT_EQ(typeTree.nodes[3].persistent.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[3].persistent[0].first),
            "InternalPersistent");
}

TEST(CodeAnalysis, TypeTreePlatform) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree =
      AST::parseFile(TESTS_SOURCE_DIR "codeanalysis/typetree_platform.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto typeTree = CodeAnalysis::getStateStructInformation(ScopeStack(), tree);

  ASSERT_EQ(typeTree.internal.size(), 2);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[0].first), "Input");
  EXPECT_EQ(typeTree.internal[0].second, "_RealType");
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[1].first), "Output");
  EXPECT_EQ(typeTree.internal[1].second, "_SwitchType");

  ASSERT_EQ(typeTree.nodes.size(), 1);

  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].instance),
            "PlatformModule");
}

TEST(CodeAnalysis, TypeTreeDomain) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "codeanalysis/typetree_domain.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto typeTree = CodeAnalysis::getStateStructInformation(ScopeStack(), tree);

  EXPECT_EQ(typeTree.nodes.size(), 1);
  auto *domainTreeNode = typeTree.getDomainRootTree("TestDomain");
  EXPECT_TRUE(domainTreeNode);
  EXPECT_EQ(ASTQuery::getNodeName(domainTreeNode->instance), "TestDomain");

  ASSERT_EQ(domainTreeNode->external.size(), 2);
  EXPECT_EQ(ASTQuery::getNodeName(domainTreeNode->external[0].first), "Out");
  EXPECT_EQ(domainTreeNode->external[0].second, "_RealType");
  EXPECT_EQ(ASTQuery::getNodeName(domainTreeNode->external[1].first), "In");
  EXPECT_EQ(domainTreeNode->external[1].second, "_RealType");

  ASSERT_EQ(domainTreeNode->internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(domainTreeNode->internal[0].first), "Value");
  EXPECT_EQ(domainTreeNode->internal[0].second, "_RealType");
}

TEST(CodeAnalysis, TypeTreeGetParentTreeForNode) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR
                        "codeanalysis/typetree_module_nested.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto typeTree = CodeAnalysis::getStateStructInformation(ScopeStack(), tree);

  // We should have at least 1 nested node in typeTree.nodes
  ASSERT_GE(typeTree.nodes.size(), 1);
  auto nestingModTree = &typeTree.nodes[0];
  EXPECT_EQ(ASTQuery::getNodeName(nestingModTree->instance), "NestingMod");

  ASSERT_GE(nestingModTree->nodes.size(), 1);
  auto nestedModTree = &nestingModTree->nodes[0];
  EXPECT_EQ(ASTQuery::getNodeName(nestedModTree->instance), "NestedMod");

  // Test 1: Get parent for a grandchild node (NestedMod)
  auto *parentOfNested = typeTree.getParentTreeForNode(nestedModTree->instance);
  ASSERT_NE(parentOfNested, nullptr);
  EXPECT_EQ(parentOfNested, nestingModTree);

  // Test 2: Get parent for a child node (NestingMod)
  auto *parentOfNesting =
      typeTree.getParentTreeForNode(nestingModTree->instance);
  ASSERT_NE(parentOfNesting, nullptr);
  EXPECT_EQ(parentOfNesting, &typeTree);

  // Test 3: Get parent for a node that is not in the tree
  auto dummyNode =
      std::make_shared<EntityNode>("DummyBlock", __FILE__, __LINE__);
  auto *parentOfDummy = typeTree.getParentTreeForNode(dummyNode);
  EXPECT_EQ(parentOfDummy, nullptr);

  // Test 4: Passing nullptr node
  auto *parentOfNull = typeTree.getParentTreeForNode(nullptr);
  EXPECT_EQ(parentOfNull, nullptr);
}

TEST(CodeAnalysis, TypeTreeReaction) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree =
      AST::parseFile(TESTS_SOURCE_DIR "codeanalysis/typetree_reaction.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto typeTree = CodeAnalysis::getStateStructInformation(ScopeStack(), tree);

  // We should have at least 1 nested node in typeTree.nodes
  ASSERT_EQ(typeTree.nodes.size(), 1);
  ASSERT_EQ(typeTree.nodes[0].nodes.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].instance), "Mod");
  ASSERT_EQ(typeTree.nodes[0].nodes[0].input.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].input[0].first),
            "ModInput");
  // ModOutput is only used inside the nested reaction
  ASSERT_EQ(typeTree.nodes[0].nodes[0].output.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].output[0].first),
            "ModOutput");
  ASSERT_EQ(typeTree.nodes[0].nodes[0].internal.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].internal[0].first),
            "ModInternal");
  // 2 external are used in reaction: ModOutput and ModInternal
  ASSERT_EQ(typeTree.nodes[0].nodes[0].nodes[0].external.size(), 2);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].nodes[0].nodes[0].instance),
            "NestedReaction");
  ASSERT_EQ(ASTQuery::getNodeName(
                typeTree.nodes[0].nodes[0].nodes[0].external[0].first),
            "ModInternal");
  // Reactions are stateless, so internals that would be persistent in modules
  // are not
  ASSERT_EQ(typeTree.nodes[0].nodes[0].nodes[0].internal.size(), 1);
  ASSERT_FALSE(typeTree.nodes[0]
                   .nodes[0]
                   .nodes[0]
                   .internal[0]
                   .first->getCompilerProperty("persistent"));
}

TEST(CodeAnalysis, TypeTreeBundle) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "codeanalysis/typetree_bundle.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto typeTree = CodeAnalysis::getStateStructInformation(ScopeStack(), tree);

  ASSERT_EQ(typeTree.internal.size(), 5);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.internal[0].first), "Input");
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.internal[1].first), "Output");
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.internal[2].first), "ListInput");
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.internal[3].first), "Index");
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.internal[4].first), "ListOutput");

  ASSERT_EQ(typeTree.nodes[0].input.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].input[0].first),
            "ModInput");
  ASSERT_EQ(typeTree.nodes[0].output.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].output[0].first),
            "ModOutput");
  ASSERT_EQ(typeTree.nodes[0].internal.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].internal[0].first),
            "ModInternal");
}

TEST(CodeAnalysis, TypeTreePortProperty) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR
                        "codeanalysis/typetree_portproperty.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto typeTree = CodeAnalysis::getStateStructInformation(ScopeStack(), tree);

  ASSERT_EQ(typeTree.nodes.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].instance), "Mod");

  ASSERT_EQ(typeTree.nodes[0].input.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].input[0].first),
            "ModInput");
  ASSERT_EQ(typeTree.nodes[0].output.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].output[0].first),
            "ModOutput");
  ASSERT_EQ(typeTree.nodes[0].internal.size(), 1);
  ASSERT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].internal[0].first),
            "ModInternal");
  ASSERT_EQ(typeTree.nodes[0].external.size(), 1);
  ASSERT_EQ(typeTree.nodes[0].external[0].first->getNodeType(),
            AST::MemberAccess);
  auto pp = std::static_pointer_cast<MemberAccessNode>(
      typeTree.nodes[0].external[0].first);

  ASSERT_EQ(pp->getEntity(), "InputPort");
  ASSERT_EQ(pp->getPropertyName(), "size");
}

TEST(CodeAnalysis, EvaluateBundleNumOutputs) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "resolver/resolve_types.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  // Pick a bundle declaration: signal IntInputBundle[2]
  auto decl =
      ASTQuery::findDeclarationByName("IntInputBundle", ScopeStack(), tree);
  ASSERT_TRUE(decl);

  std::vector<LangError> errors;
  int bundleSize =
      CodeAnalysis::getNodeNumOutputs(decl, ScopeStack(), tree, &errors);
  EXPECT_EQ(bundleSize, 2);
}

TEST(CodeAnalysis, EvaluateSizePortProperty) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "resolver/compiler_properties.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  // Find a module definition with ports
  bool checked = false;
  for (const auto &node : tree->getChildren()) {
    if (node->getLine() == 23) {
      EXPECT_EQ(node->getNodeType(), AST::Stream);
      auto stream = std::static_pointer_cast<StreamNode>(node);
      auto funcNode = stream->getRight()->getChildren()[0];
      EXPECT_TRUE(funcNode);
      EXPECT_EQ(funcNode->getNodeType(), AST::Function);
      auto functionNode = std::static_pointer_cast<FunctionNode>(funcNode);

      std::string framework;
      auto funcDecl = ASTQuery::findDeclarationByName(
          functionNode->getName(), ScopeStack(), tree,
          functionNode->getNamespaceList(), framework);

      if (funcDecl) {
        int size = CodeAnalysis::evaluateSizeMemberAccess(
            "InputPort", ScopeStack(), funcDecl, functionNode, tree);
        EXPECT_GE(size, -2);
        checked = true;
      }
    }
  }
  EXPECT_TRUE(checked);
}

TEST(CodeAnalysis, StateStructInformationForDeclaration) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR
                                "codeanalysis/typetree_module_nested.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto decl =
      ASTQuery::findDeclarationByName("NestingMod", ScopeStack(), tree);
  ASSERT_TRUE(decl != nullptr);

  auto typeTree =
      CodeAnalysis::getStateStructInformationForDeclaration(decl, ScopeStack(), tree);

  EXPECT_EQ(typeTree.instance, decl);
  ASSERT_EQ(typeTree.input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.input[0].first), "NestingModInput");
  ASSERT_EQ(typeTree.output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.output[0].first), "NestingModOutput");
  ASSERT_EQ(typeTree.internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.internal[0].first), "NestingModInternal");

  ASSERT_EQ(typeTree.nodes.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].instance), "NestedMod");
  ASSERT_EQ(typeTree.nodes[0].input.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].input[0].first), "NestedModInput");
  ASSERT_EQ(typeTree.nodes[0].output.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].output[0].first), "NestedModOutput");
  ASSERT_EQ(typeTree.nodes[0].internal.size(), 1);
  EXPECT_EQ(ASTQuery::getNodeName(typeTree.nodes[0].internal[0].first), "NestedModInternal");
}

TEST(CodeAnalysis, MatchDefinitionToTypesOverloadResolution) {
  auto makePlatformModule = [](const std::string &name,
                               const std::vector<std::string> &inputTypes) {
    auto decl = std::make_shared<DeclarationNode>(name, "platformModule", nullptr,
                                                  __FILE__, __LINE__);
    auto inputsList = std::make_shared<ListNode>(__FILE__, __LINE__);
    for (const auto &t : inputTypes) {
      inputsList->addChild(
          std::make_shared<EntityNode>(t, __FILE__, __LINE__));
    }
    decl->addProperty(std::make_shared<PropertyNode>("inputs", inputsList,
                                                     __FILE__, __LINE__));
    return decl;
  };

  // Equal candidates: Equal@String_Bool is placed FIRST to verify ordering independence
  auto equalString =
      makePlatformModule("Equal@String_Bool", {"_StringType", "_StringType"});
  auto equalDouble =
      makePlatformModule("Equal@Double_Bool", {"_RealType", "_RealType"});
  auto equalInt =
      makePlatformModule("Equal@Int_Bool", {"_IntType", "_IntType"});

  std::vector<std::shared_ptr<DeclarationNode>> candidates = {equalString,
                                                              equalDouble,
                                                              equalInt};

  // 1. Real + Real inputs -> matches Equal@Double_Bool
  {
    auto func =
        std::make_shared<FunctionNode>("Equal", nullptr, __FILE__, __LINE__);
    auto inputList = std::make_shared<ListNode>(__FILE__, __LINE__);
    inputList->addChild(std::make_shared<ValueNode>(1.5, __FILE__, __LINE__));
    inputList->addChild(std::make_shared<ValueNode>(2.5, __FILE__, __LINE__));
    func->setCompilerProperty("mainInput", inputList);

    auto matched = CodeAnalysis::matchDefinitionToTypes(candidates, func);
    ASSERT_TRUE(matched != nullptr);
    EXPECT_EQ(matched->getName(), "Equal@Double_Bool");
  }

  // 2. Real + Int inputs with Int marked with "typecast" = "_RealType" -> matches Equal@Double_Bool
  {
    auto func =
        std::make_shared<FunctionNode>("Equal", nullptr, __FILE__, __LINE__);
    auto inputList = std::make_shared<ListNode>(__FILE__, __LINE__);
    inputList->addChild(std::make_shared<ValueNode>(1.5, __FILE__, __LINE__));
    auto intElem = std::make_shared<ValueNode>(int64_t(0), __FILE__, __LINE__);
    intElem->setCompilerProperty(
        "typecast",
        std::make_shared<ValueNode>("_RealType", __FILE__, __LINE__));
    inputList->addChild(intElem);
    func->setCompilerProperty("mainInput", inputList);

    auto matched = CodeAnalysis::matchDefinitionToTypes(candidates, func);
    ASSERT_TRUE(matched != nullptr);
    EXPECT_EQ(matched->getName(), "Equal@Double_Bool");
  }

  // 3. Real + Int inputs without explicit typecast (int-to-real promotion) -> matches Equal@Double_Bool
  {
    auto func =
        std::make_shared<FunctionNode>("Equal", nullptr, __FILE__, __LINE__);
    auto inputList = std::make_shared<ListNode>(__FILE__, __LINE__);
    inputList->addChild(std::make_shared<ValueNode>(1.5, __FILE__, __LINE__));
    inputList->addChild(
        std::make_shared<ValueNode>(int64_t(0), __FILE__, __LINE__));
    func->setCompilerProperty("mainInput", inputList);

    auto matched = CodeAnalysis::matchDefinitionToTypes(candidates, func);
    ASSERT_TRUE(matched != nullptr);
    EXPECT_EQ(matched->getName(), "Equal@Double_Bool");
  }

  // 4. Int + Int inputs -> matches Equal@Int_Bool
  {
    auto func =
        std::make_shared<FunctionNode>("Equal", nullptr, __FILE__, __LINE__);
    auto inputList = std::make_shared<ListNode>(__FILE__, __LINE__);
    inputList->addChild(
        std::make_shared<ValueNode>(int64_t(10), __FILE__, __LINE__));
    inputList->addChild(
        std::make_shared<ValueNode>(int64_t(20), __FILE__, __LINE__));
    func->setCompilerProperty("mainInput", inputList);

    auto matched = CodeAnalysis::matchDefinitionToTypes(candidates, func);
    ASSERT_TRUE(matched != nullptr);
    EXPECT_EQ(matched->getName(), "Equal@Int_Bool");
  }

  // 5. String + String inputs -> matches Equal@String_Bool
  {
    auto func =
        std::make_shared<FunctionNode>("Equal", nullptr, __FILE__, __LINE__);
    auto inputList = std::make_shared<ListNode>(__FILE__, __LINE__);
    inputList->addChild(
        std::make_shared<ValueNode>(std::string("hello"), __FILE__, __LINE__));
    inputList->addChild(
        std::make_shared<ValueNode>(std::string("world"), __FILE__, __LINE__));
    func->setCompilerProperty("mainInput", inputList);

    auto matched = CodeAnalysis::matchDefinitionToTypes(candidates, func);
    ASSERT_TRUE(matched != nullptr);
    EXPECT_EQ(matched->getName(), "Equal@String_Bool");
  }

  // 6. Single input overload: Overload@Double vs Overload@Int
  {
    auto overloadDouble =
        makePlatformModule("Overload@Double", {"_RealType"});
    auto overloadInt = makePlatformModule("Overload@Int", {"_IntType"});
    std::vector<std::shared_ptr<DeclarationNode>> unaryCandidates = {
        overloadDouble, overloadInt};

    auto funcInt =
        std::make_shared<FunctionNode>("Overload", nullptr, __FILE__, __LINE__);
    funcInt->setCompilerProperty(
        "mainInput",
        std::make_shared<ValueNode>(int64_t(42), __FILE__, __LINE__));
    auto matchedInt =
        CodeAnalysis::matchDefinitionToTypes(unaryCandidates, funcInt);
    ASSERT_TRUE(matchedInt != nullptr);
    EXPECT_EQ(matchedInt->getName(), "Overload@Int");

    auto funcReal =
        std::make_shared<FunctionNode>("Overload", nullptr, __FILE__, __LINE__);
    funcReal->setCompilerProperty(
        "mainInput", std::make_shared<ValueNode>(3.14, __FILE__, __LINE__));
    auto matchedReal =
        CodeAnalysis::matchDefinitionToTypes(unaryCandidates, funcReal);
    ASSERT_TRUE(matchedReal != nullptr);
    EXPECT_EQ(matchedReal->getName(), "Overload@Double");
  }

  // 7. Input entity resolved via resolveConnectionEntity when input has connected signal entity
  {
    auto signalDecl = std::make_shared<DeclarationNode>(
        "MyRealSignal", "signal", nullptr, __FILE__, __LINE__);
    signalDecl->addProperty(std::make_shared<PropertyNode>(
        "type", std::make_shared<EntityNode>("_RealType", __FILE__, __LINE__),
        __FILE__, __LINE__));

    auto entityNode =
        std::make_shared<EntityNode>("MyRealSignal", __FILE__, __LINE__);
    entityNode->setCompilerProperty("declaration", signalDecl);

    auto func =
        std::make_shared<FunctionNode>("Equal", nullptr, __FILE__, __LINE__);
    auto inputList = std::make_shared<ListNode>(__FILE__, __LINE__);
    inputList->addChild(entityNode);
    inputList->addChild(std::make_shared<ValueNode>(1.0, __FILE__, __LINE__));
    func->setCompilerProperty("mainInput", inputList);

    auto matched = CodeAnalysis::matchDefinitionToTypes(candidates, func);
    ASSERT_TRUE(matched != nullptr);
    EXPECT_EQ(matched->getName(), "Equal@Double_Bool");
  }
}

TEST(CodeAnalysis, GeneratorRangeDataType) {
  // Direct GeneratorRangeNode
  auto start = std::make_shared<ValueNode>(int64_t(0), __FILE__, __LINE__);
  auto end = std::make_shared<ValueNode>(int64_t(8), __FILE__, __LINE__);
  auto genNode = std::make_shared<GeneratorRangeNode>(start, end, __FILE__, __LINE__);
  EXPECT_EQ(CodeAnalysis::resolveNodeOutDataType(genNode, ScopeStack(), nullptr), "_IntType");

  // Stepped GeneratorRangeNode
  auto step = std::make_shared<ValueNode>(int64_t(2), __FILE__, __LINE__);
  auto genStepped = std::make_shared<GeneratorRangeNode>(start, step, end, __FILE__, __LINE__);
  EXPECT_EQ(CodeAnalysis::resolveNodeOutDataType(genStepped, ScopeStack(), nullptr), "_IntType");

  // ListNode containing GeneratorRangeNode
  auto listNode = std::make_shared<ListNode>(__FILE__, __LINE__);
  listNode->addChild(genNode);
  EXPECT_EQ(CodeAnalysis::resolveNodeOutDataType(listNode, ScopeStack(), nullptr), "_IntType");

  // ListNode containing mixed int and GeneratorRangeNode
  auto mixedList = std::make_shared<ListNode>(__FILE__, __LINE__);
  mixedList->addChild(std::make_shared<ValueNode>(int64_t(1), __FILE__, __LINE__));
  mixedList->addChild(genNode);
  EXPECT_EQ(CodeAnalysis::resolveNodeOutDataType(mixedList, ScopeStack(), nullptr), "_IntType");
}


