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

#include <algorithm>

#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridegen/coderesolver.hpp"
#include "stride/parser/functionnode.h"
#include "stride/parser/generatorrangenode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/platformnode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/astvalidation.h"
// #include "stride/stridegen/stridelibrary.hpp"

#include <fstream>
#include <functional>

using namespace strd;

std::string writeCodeToFile(std::string code) {
  std::string fileName = "temp.stride";
  std::ofstream output_file(fileName);

  EXPECT_TRUE(output_file.is_open());

  output_file << code;
  output_file.close();
  return fileName;
}

TEST(Resolver, ResolveBlockPersistence) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "resolver/block_persistence.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();
  {
    auto modDecl = ASTQuery::findDeclarationByName("Test", ScopeStack(), tree);
    EXPECT_TRUE(modDecl);
    auto blocks = modDecl->getPropertyValue("blocks");

    auto decl =
        ASTQuery::findDeclarationByName("WrittenFirst", ScopeStack(), blocks);
    ASSERT_TRUE(decl);
    auto persistentProp = decl->getCompilerProperty("persistent");
    EXPECT_FALSE(persistentProp);
    decl = ASTQuery::findDeclarationByName("ReadFirst", ScopeStack(), blocks);
    ASSERT_TRUE(decl);
    persistentProp = decl->getCompilerProperty("persistent");
    ASSERT_TRUE(persistentProp);
    EXPECT_EQ(persistentProp->getNodeType(), AST::Switch);
    EXPECT_TRUE(
        std::static_pointer_cast<ValueNode>(persistentProp)->getSwitchValue());
  }
  // TODO resolve persistence recursively in function instances
  // https://github.com/StrideLang/stridegen/issues/4
}

TEST(Resolver, ResolveTypes) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "resolver/resolve_types.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  // ForwardModuleIn >> PassThroughForward() >> ForwardModuleOut; # nothing
  // should be resolved here
  //     ForwardModuleInInt >> PassTPassThroughForwardhrough() >>
  //     ForwardModuleOutResolved; # forward resolve
  // ForwardModuleInNotResolved >>
  //     PassThroughForward() >> ForwardModuleOutInt; # reverse resolve, not
  //     resolved
  {
    auto decl =
        ASTQuery::findDeclarationByName("ForwardModuleOut", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    // Not resolved from connections: default from the type class
    ASSERT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Real");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("ForwardModuleOutResolved",
                                                ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    // FIXME
    // EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
    //           "Int");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("ForwardModuleInNotResolved",
                                                ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    // Not resolved from connections: default from the type class
    ASSERT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Real");
  }
  // ModuleIn >> PassThrough() >> ModuleOut; # nothing should be resolved here
  // ModuleInInt >> PassThrough() >> ModuleOutNotResolved; # forward resolve,
  // not resolved ModuleInResolved >> PassThrough() >> ModuleOutInt; # reverse
  // resolve
  {
    auto decl =
        ASTQuery::findDeclarationByName("ModuleOut", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    // Not resolved from connections: default from the type class
    ASSERT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Real");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("ModuleOutNotResolved",
                                                ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    // Not resolved from connections: default from the type class
    ASSERT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Real");
  }
  {
    auto decl =
        ASTQuery::findDeclarationByName("ModuleInResolved", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }
  // ModuleInInt >> PassThroughWithType() >> ModuleWithTypeOutResolved;
  // ModuleWithTypeInResolved >> PassThroughWithType() >> ModuleOutInt;
  {
    auto decl = ASTQuery::findDeclarationByName("ModuleWithTypeOutResolved",
                                                ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Real");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("ModuleWithTypeInResolved",
                                                ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Real");
  }
  // Forward resolution
  {
    auto decl = ASTQuery::findDeclarationByName("Output", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("Input", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("OutputB2", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("InputA2", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }
  // Bundles
  {
    auto decl =
        ASTQuery::findDeclarationByName("OutputBundle", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }
  {
    auto decl =
        ASTQuery::findDeclarationByName("InputBundle", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("OutputBundleMember",
                                                ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    ASSERT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("InputBundleMember",
                                                ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    EXPECT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Int");
  }

  // ModuleIn >> PassThrough() >> ModuleOut; # nothing should be resolved here
  // ModuleInInt >> PassThrough() >> ModuleOutResolved; # forward resolve
  // ModuleInResolved >> PassThrough() >> ModuleOutInt; # reverse resolve
  {
    auto decl =
        ASTQuery::findDeclarationByName("ModuleOut", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    // Not resolved from connections: default from the type class
    ASSERT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Real");
  }
  {
    auto decl = ASTQuery::findDeclarationByName("ModuleIn", ScopeStack(), tree);
    EXPECT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    EXPECT_TRUE(dataType);
    // Not resolved from connections: default from the type class
    ASSERT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(std::static_pointer_cast<EntityNode>(dataType)->getName(),
              "Real");
  }
}

TEST(Resolver, TypeClassDefaults) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "resolver/type_class.stride");
  ASSERT_TRUE(tree != nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  // _Typed lists _NumericTypes in allowedTypes for its type property
  EXPECT_TRUE(ASTQuery::findTypeClass("_NumericTypes", tree));
  {
    // Default comes from the first member of the typeClass
    auto decl = ASTQuery::findDeclarationByName("Untyped", ScopeStack(), tree);
    ASSERT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    ASSERT_TRUE(dataType);
    ASSERT_EQ(dataType->getNodeType(), AST::Entity);
    EXPECT_EQ(ASTQuery::getNodeName(dataType), "Real");
  }
  {
    auto decl =
        ASTQuery::findDeclarationByName("IntSignal", ScopeStack(), tree);
    ASSERT_TRUE(decl);
    auto dataType = decl->getPropertyValue("type");
    ASSERT_TRUE(dataType);
    EXPECT_EQ(ASTQuery::getNodeName(dataType), "Int");
  }
  {
    // switch has a builtin data type and no type property
    auto decl = ASTQuery::findDeclarationByName("Flag", ScopeStack(), tree);
    ASSERT_TRUE(decl);
    EXPECT_FALSE(decl->getPropertyValue("type"));
  }
  // Undeclared blocks receiving Switch are declared as switches
  for (const auto &name :
       {"AutoSwitch", "AutoSwitchFromFlag", "AutoSwitchFromPlatform"}) {
    auto decl = ASTQuery::findDeclarationByName(name, ScopeStack(), tree);
    ASSERT_TRUE(decl) << name;
    EXPECT_EQ(decl->getEntityType(), "switch") << name;
    EXPECT_FALSE(decl->getPropertyValue("type")) << name;
    auto defaultNode = decl->getPropertyValue("default");
    ASSERT_TRUE(defaultNode) << name;
    EXPECT_EQ(defaultNode->getNodeType(), AST::Switch) << name;
  }
}

TEST(Resolver, TypeClassRejectsNonMember) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "resolver/type_class.stride");
  ASSERT_TRUE(tree != nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  // Only "signal SwitchSignal { type: Switch }" is invalid, as
  // Switch is not a member of _NumericTypes
  auto errors = ASTValidation::validate(tree);
  int typeErrors = 0;
  int unresolvedErrors = 0;
  for (const auto &error : errors) {
    if (error.type == LangError::InvalidPortType &&
        error.errorTokens.size() == 4 && error.errorTokens[1] == "type") {
      EXPECT_EQ(error.errorTokens[0], "signal");
      EXPECT_NE(error.errorTokens[3].find("Real"), std::string::npos);
      typeErrors++;
    } else if (error.type == LangError::UnresolvedType) {
      // FromOutput takes its output type from its output port
      ASSERT_EQ(error.errorTokens.size(), 2);
      EXPECT_EQ(error.errorTokens[0], "Unresolvable");
      EXPECT_EQ(error.errorTokens[1], "FromOutput");
      unresolvedErrors++;
    }
  }
  EXPECT_EQ(typeErrors, 1);
  EXPECT_EQ(unresolvedErrors, 1);
}

TEST(Resolver, ReadOnlyPortAttributes) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "resolver/read_only_port.stride");
  ASSERT_TRUE(tree != nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  // Only the type written on InputPort is an error. The port attributes
  // filled in by the compiler on both ports are not.
  auto errors = ASTValidation::validate(tree);
  int readOnlyErrors = 0;
  for (const auto &error : errors) {
    if (error.type == LangError::ReadOnlyPort) {
      ASSERT_EQ(error.errorTokens.size(), 2);
      EXPECT_EQ(error.errorTokens[0], "mainInputPort");
      EXPECT_EQ(error.errorTokens[1], "type");
      readOnlyErrors++;
    }
  }
  EXPECT_EQ(readOnlyErrors, 1);
}

TEST(Resolver, MultiInstanceStreamContinues) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "resolver/broadcast_tail.stride");
  ASSERT_TRUE(tree != nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  std::function<bool(ASTNode, const std::string &)> containsName =
      [&](ASTNode node, const std::string &name) {
        if (!node) {
          return false;
        }
        if ((node->getNodeType() == AST::Function ||
             node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) &&
            ASTQuery::getNodeName(node) == name) {
          return true;
        }
        if (node->getNodeType() == AST::Stream) {
          auto stream = std::static_pointer_cast<StreamNode>(node);
          return containsName(stream->getLeft(), name) ||
                 containsName(stream->getRight(), name);
        }
        for (const auto &child : node->getChildren()) {
          if (containsName(child, name)) {
            return true;
          }
        }
        return false;
      };

  int isZeroStreams = 0;
  int allZeroStreams = 0;
  int allZeroDirectStreams = 0;
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() != AST::Stream) {
      continue;
    }
    if (containsName(node, "IsZero")) {
      isZeroStreams++;
      // Each instance only writes its output, the rest of the stream is
      // a separate stream
      EXPECT_FALSE(containsName(node, "AllOn"));
    }
    if (containsName(node, "AllZero")) {
      allZeroStreams++;
      EXPECT_TRUE(containsName(node, "Zero"));
    }
    if (containsName(node, "AllZeroDirect")) {
      allZeroDirectStreams++;
    }
  }
  EXPECT_EQ(isZeroStreams, 6); // 3 instances in each stream
  EXPECT_EQ(allZeroStreams, 1);
  EXPECT_EQ(allZeroDirectStreams, 1);

  // The instance outputs of the second stream go to a declared bundle, a
  // switch as IsZero outputs Switch
  std::shared_ptr<DeclarationNode> bundle;
  for (const auto &node : tree->getChildren()) {
    if (node->getNodeType() == AST::ArrayDeclaration &&
        std::static_pointer_cast<DeclarationNode>(node)->getName().rfind(
            "__IsZero_Out", 0) == 0) {
      bundle = std::static_pointer_cast<DeclarationNode>(node);
    }
  }
  ASSERT_TRUE(bundle);
  EXPECT_EQ(bundle->getEntityType(), "switch");
}

TEST(Resolver, SelfPort) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "resolver/01_self_port.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto decl = ASTQuery::findDeclarationByName("TypeTest", ScopeStack(), tree);
  EXPECT_TRUE(decl);
  auto filledValue = decl->getPropertyValue("values");
  EXPECT_TRUE(filledValue);
  EXPECT_EQ(filledValue->getNodeType(), AST::List);
  EXPECT_EQ(filledValue->getChildren().size(), 9);
}

TEST(Resolver, SynthesizeBundleDeclarationMember) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "resolver/01_self_port.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto decl = ASTQuery::findDeclarationByName("TypeTest", ScopeStack(), tree);
  EXPECT_TRUE(decl);
  auto elementDecl =
      ASTQuery::synthesizeArrayDeclarationElement(decl, 3, ScopeStack(), tree);
  EXPECT_TRUE(elementDecl);
  EXPECT_EQ(elementDecl->getNodeType(), AST::Declaration);
  auto elementValue = elementDecl->getPropertyValue("values");
  EXPECT_TRUE(elementValue);
  EXPECT_EQ(elementValue->getNodeType(), AST::Int);
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(elementValue)->getIntValue(),
            3);
}

TEST(Resolver, SynthesizeBundleDeclarationMemberNested) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "resolver/01_self_port.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto decl =
      ASTQuery::findDeclarationByName("NestedTypeTest", ScopeStack(), tree);
  EXPECT_TRUE(decl);
  auto elementDecl =
      ASTQuery::synthesizeArrayDeclarationElement(decl, 3, ScopeStack(), tree);
  ASSERT_TRUE(elementDecl);
  EXPECT_EQ(elementDecl->getNodeType(), AST::Declaration);
  auto elementValue = elementDecl->getPropertyValue("values");
  // EXPECT_TRUE(elementValue);
  // EXPECT_EQ(elementValue->getNodeType(), AST::Int);
  // EXPECT_EQ(std::static_pointer_cast<ValueNode>(elementValue)->getIntValue(),
  //           3);
}

TEST(Resolver, GeneratorRangeExpansionInStreams) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();

  // Bare generator 0..8 >> Target should not be allowed
  {
    std::string invalidCode = "signal Target[9] {}\n0..8 >> Target;\n";
    std::string invalidFile = writeCodeToFile(invalidCode);
    ASTNode invalidTree = AST::parseFile(invalidFile.c_str());
    EXPECT_EQ(invalidTree, nullptr);
  }

  std::string code = R"(
signal Target1[9] {}
[0..8] >> Target1;

signal Target2[5] {}
[0..4] >> Target2;

signal Target3[4] {}
[1, 2..3, 10] >> Target3;
)";
  std::string file = writeCodeToFile(code);
  ASTNode tree = AST::parseFile(file.c_str());
  ASSERT_NE(tree, nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  std::vector<std::shared_ptr<StreamNode>> streams;
  for (const auto &child : tree->getChildren()) {
    if (child->getNodeType() == AST::Stream) {
      streams.push_back(std::static_pointer_cast<StreamNode>(child));
    }
  }
  ASSERT_GE(streams.size(), 3);

  // Stream 0: [0..8] >> Target1;
  // CodeResolver expands [0..8] into a ListNode of 9 ValueNodes [0..8]
  auto left0 = streams[0]->getLeft();
  ASSERT_NE(left0, nullptr);
  ASSERT_EQ(left0->getNodeType(), AST::List);
  auto list0 = std::static_pointer_cast<ListNode>(left0);
  ASSERT_EQ(list0->getChildren().size(), 9);
  for (int64_t i = 0; i <= 8; ++i) {
    ASSERT_EQ(list0->getChildren().at(i)->getNodeType(), AST::Int);
    EXPECT_EQ(std::static_pointer_cast<ValueNode>(list0->getChildren().at(i))->getIntValue(), i);
  }

  // Stream 1: [0..4] >> Target2;
  auto left1 = streams[1]->getLeft();
  ASSERT_NE(left1, nullptr);
  ASSERT_EQ(left1->getNodeType(), AST::List);
  auto list1 = std::static_pointer_cast<ListNode>(left1);
  ASSERT_EQ(list1->getChildren().size(), 5);
  for (int64_t i = 0; i <= 4; ++i) {
    ASSERT_EQ(list1->getChildren().at(i)->getNodeType(), AST::Int);
    EXPECT_EQ(std::static_pointer_cast<ValueNode>(list1->getChildren().at(i))->getIntValue(), i);
  }

  // Stream 2: [1, 2..3, 10] >> Target3;
  // Flattens into [1, 2, 3, 10]
  auto left2 = streams[2]->getLeft();
  ASSERT_NE(left2, nullptr);
  ASSERT_EQ(left2->getNodeType(), AST::List);
  auto list2 = std::static_pointer_cast<ListNode>(left2);
  ASSERT_EQ(list2->getChildren().size(), 4);
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(list2->getChildren().at(0))->getIntValue(), 1);
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(list2->getChildren().at(1))->getIntValue(), 2);
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(list2->getChildren().at(2))->getIntValue(), 3);
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(list2->getChildren().at(3))->getIntValue(), 10);
}

TEST(Resolver, GeneratorRangeInProperties) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "resolver/generator_in_properties.stride");
  ASSERT_NE(tree, nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto modDecl = ASTQuery::findDeclarationByName("ModRange", ScopeStack(), tree);
  ASSERT_NE(modDecl, nullptr);
  auto modRange = modDecl->getPropertyValue("range");
  ASSERT_NE(modRange, nullptr);
  ASSERT_EQ(modRange->getNodeType(), AST::List);
  auto modList = std::static_pointer_cast<ListNode>(modRange);
  ASSERT_EQ(modList->getChildren().size(), 1);
  ASSERT_EQ(modList->getChildren().at(0)->getNodeType(), AST::GeneratorRange);
  auto modGen = std::static_pointer_cast<GeneratorRangeNode>(modList->getChildren().at(0));
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(modGen->startIndex())->getIntValue(), 0);
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(modGen->endIndex())->getIntValue(), 8);

  auto modRangeList = modDecl->getPropertyValue("listRange");
  ASSERT_NE(modRangeList, nullptr);
  ASSERT_EQ(modRangeList->getNodeType(), AST::List);
  auto modListRange = std::static_pointer_cast<ListNode>(modRangeList);
  ASSERT_EQ(modListRange->getChildren().size(), 1);
  ASSERT_EQ(modListRange->getChildren().at(0)->getNodeType(), AST::GeneratorRange);

  // Ranges in a signal's default and a constant's value are expanded so there
  // is one value per element.
  auto expectValues = [](std::shared_ptr<DeclarationNode> decl,
                         const std::string &propertyName,
                         const std::vector<int64_t> &expected) {
    ASSERT_NE(decl, nullptr);
    auto values = decl->getPropertyValue(propertyName);
    ASSERT_NE(values, nullptr);
    ASSERT_EQ(values->getNodeType(), AST::List);
    auto children = values->getChildren();
    ASSERT_EQ(children.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      ASSERT_EQ(children.at(i)->getNodeType(), AST::Int);
      EXPECT_EQ(std::static_pointer_cast<ValueNode>(children.at(i))->getIntValue(),
                expected[i]);
    }
  };
  auto expectDefault = [&](std::shared_ptr<DeclarationNode> decl,
                           const std::vector<int64_t> &expected) {
    expectValues(decl, "default", expected);
  };
  expectValues(ASTQuery::findDeclarationByName("RangeConst", ScopeStack(), tree),
               "value", {0, 1, 2, 3, 4, 5, 6, 7, 8});
  expectValues(ASTQuery::findDeclarationByName("ConstTable", ScopeStack(), tree),
               "value", {0, 1, 2, 7, 9});

  // An element of a constant array with a constant index is folded.
  bool foundConstElement = false;
  for (const auto &child : tree->getChildren()) {
    if (child->getNodeType() != AST::Stream) {
      continue;
    }
    auto stream = std::static_pointer_cast<StreamNode>(child);
    auto right = stream->getRight();
    if (right && right->getNodeType() == AST::Entity &&
        std::static_pointer_cast<EntityNode>(right)->getName() == "ConstOut") {
      foundConstElement = true;
      ASSERT_EQ(stream->getLeft()->getNodeType(), AST::Int);
      EXPECT_EQ(std::static_pointer_cast<ValueNode>(stream->getLeft())
                    ->getIntValue(),
                7);
    }
  }
  EXPECT_TRUE(foundConstElement);
  expectDefault(ASTQuery::findDeclarationByName("TestSig", ScopeStack(), tree),
                {0, 1, 2, 3, 4, 5, 6, 7, 8});
  expectDefault(ASTQuery::findDeclarationByName("MixedSig", ScopeStack(), tree),
                {0, 1, 2, 7, 10, 12, 14, 0, 1});
  expectDefault(
      ASTQuery::findDeclarationByName("DescendingSig", ScopeStack(), tree),
      {3, 2, 1, 9});
  auto blockMod = ASTQuery::findDeclarationByName("BlockRange", ScopeStack(), tree);
  ASSERT_NE(blockMod, nullptr);
  expectDefault(ASTQuery::findDeclarationByName(
                    "LocalSig", ScopeStack(), blockMod->getPropertyValue("blocks")),
                {5, 6, 7, 8});

  bool foundFunc = false;
  for (const auto &child : tree->getChildren()) {
    if (child->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(child);
      auto checkFunc = [&](ASTNode node) {
        if (node && node->getNodeType() == AST::Function) {
          auto func = std::static_pointer_cast<FunctionNode>(node);
          if (func->getName() == "Func") {
            foundFunc = true;
            auto rangeProp = func->getPropertyValue("range");
            ASSERT_NE(rangeProp, nullptr);
            ASSERT_EQ(rangeProp->getNodeType(), AST::List);
            auto funcList = std::static_pointer_cast<ListNode>(rangeProp);
            ASSERT_EQ(funcList->getChildren().size(), 1);
            ASSERT_EQ(funcList->getChildren().at(0)->getNodeType(), AST::GeneratorRange);
            auto gen = std::static_pointer_cast<GeneratorRangeNode>(funcList->getChildren().at(0));
            EXPECT_EQ(std::static_pointer_cast<ValueNode>(gen->startIndex())->getIntValue(), 0);
            EXPECT_EQ(std::static_pointer_cast<ValueNode>(gen->endIndex())->getIntValue(), 8);
          }
        }
      };
      checkFunc(stream->getLeft());
      checkFunc(stream->getRight());
    }
  }
  EXPECT_TRUE(foundFunc);
}

TEST(Resolver, ResolveMultiRateStreams) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  std::string code = R"(
_domainDefinition RateDom {
    rate: 100
}
signal FastIn { type: Int default: 0 rate: 400 domain: RateDom }
signal FastMid { type: Int default: 0 domain: RateDom }
signal SlowOut { type: Int default: 0 rate: 25 domain: RateDom }
signal UnratedA { type: Int default: 0 domain: RateDom }
signal UnratedB { type: Int default: 0 domain: RateDom }
signal ReverseRatedOut { type: Int default: 0 rate: 50 domain: RateDom }
signal DefaultDomA { type: Int default: 0 domain: RateDom }
signal DefaultDomB { type: Int default: 0 domain: RateDom }

FastIn >> FastMid >> SlowOut;
UnratedA >> UnratedB >> ReverseRatedOut;
DefaultDomA >> DefaultDomB;
)";
  std::string fileName = "temp_multirate_streams.stride";
  {
    std::ofstream out(fileName);
    out << code;
  }
  ASTNode tree = AST::parseFile(fileName.c_str());
  ASSERT_NE(tree, nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  ScopeStack scope;
  auto fastIn = ASTQuery::findDeclarationByName("FastIn", scope, tree);
  ASSERT_NE(fastIn, nullptr);
  EXPECT_DOUBLE_EQ(CodeAnalysis::getNodeRate(fastIn, scope, tree), 400.0);

  auto fastMid = ASTQuery::findDeclarationByName("FastMid", scope, tree);
  ASSERT_NE(fastMid, nullptr);
  EXPECT_DOUBLE_EQ(CodeAnalysis::getNodeRate(fastMid, scope, tree), 400.0);

  auto slowOut = ASTQuery::findDeclarationByName("SlowOut", scope, tree);
  ASSERT_NE(slowOut, nullptr);
  EXPECT_DOUBLE_EQ(CodeAnalysis::getNodeRate(slowOut, scope, tree), 25.0);

  auto unratedA = ASTQuery::findDeclarationByName("UnratedA", scope, tree);
  ASSERT_NE(unratedA, nullptr);
  EXPECT_DOUBLE_EQ(CodeAnalysis::getNodeRate(unratedA, scope, tree), 50.0);

  auto unratedB = ASTQuery::findDeclarationByName("UnratedB", scope, tree);
  ASSERT_NE(unratedB, nullptr);
  EXPECT_DOUBLE_EQ(CodeAnalysis::getNodeRate(unratedB, scope, tree), 50.0);

  auto defaultDomA = ASTQuery::findDeclarationByName("DefaultDomA", scope, tree);
  ASSERT_NE(defaultDomA, nullptr);
  EXPECT_DOUBLE_EQ(CodeAnalysis::getNodeRate(defaultDomA, scope, tree), 100.0);

  auto defaultDomB = ASTQuery::findDeclarationByName("DefaultDomB", scope, tree);
  ASSERT_NE(defaultDomB, nullptr);
  EXPECT_DOUBLE_EQ(CodeAnalysis::getNodeRate(defaultDomB, scope, tree), 100.0);
}

TEST(Resolver, StreamDependencyDAG) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  std::string code = R"(
_domainDefinition RateDom {
    rate: 100
}
signal In1 { type: Int default: 0 rate: 400 domain: RateDom }
signal Mid1 { type: Int default: 0 domain: RateDom }
signal In2 { type: Int default: 0 rate: 400 domain: RateDom }
signal Mid2 { type: Int default: 0 domain: RateDom }
signal Out1 { type: Int default: 0 rate: 25 domain: RateDom }
signal Out2 { type: Int default: 0 rate: 25 domain: RateDom }

In1 >> Mid1;
In2 >> Mid2;
Mid1 >> Out1;
Mid2 >> Out2;
)";
  std::string fileName = "temp_stream_dag.stride";
  {
    std::ofstream out(fileName);
    out << code;
  }
  ASTNode tree = AST::parseFile(fileName.c_str());
  ASSERT_NE(tree, nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  ScopeStack scope;
  auto domDecl = ASTQuery::findDeclarationByName("RateDom", scope, tree);
  ASSERT_NE(domDecl, nullptr);
  auto stagesProp = domDecl->getCompilerProperty("streamStages");
  ASSERT_NE(stagesProp, nullptr);
  EXPECT_EQ(stagesProp->getChildren().size(), 2u);

  // Stage 0: 400 Hz (In1 >> Mid1, In2 >> Mid2)
  auto stage0 = stagesProp->getChildren()[0];
  ASSERT_NE(stage0, nullptr);
  EXPECT_EQ(stage0->getChildren().size(), 2u);
  auto rate0 = stage0->getCompilerProperty("rate");
  ASSERT_NE(rate0, nullptr);
  EXPECT_DOUBLE_EQ(std::static_pointer_cast<ValueNode>(rate0)->toReal(), 400.0);

  // Stage 1: 25 Hz (Mid1 >> Out1, Mid2 >> Out2)
  auto stage1 = stagesProp->getChildren()[1];
  ASSERT_NE(stage1, nullptr);
  EXPECT_EQ(stage1->getChildren().size(), 2u);
  auto rate1 = stage1->getCompilerProperty("rate");
  ASSERT_NE(rate1, nullptr);
  EXPECT_DOUBLE_EQ(std::static_pointer_cast<ValueNode>(rate1)->toReal(), 25.0);

  // Verify StreamNode DAG properties
  auto stream0 = stage0->getChildren()[0];
  auto reads0 = stream0->getCompilerProperty("reads");
  auto writes0 = stream0->getCompilerProperty("writes");
  auto deps0 = stream0->getCompilerProperty("dependencies");
  ASSERT_NE(reads0, nullptr);
  ASSERT_NE(writes0, nullptr);
  ASSERT_NE(deps0, nullptr);
  EXPECT_EQ(deps0->getChildren().size(), 0u);

  auto stream2 = stage1->getChildren()[0];
  auto deps2 = stream2->getCompilerProperty("dependencies");
  ASSERT_NE(deps2, nullptr);
  EXPECT_EQ(deps2->getChildren().size(), 1u);
  EXPECT_EQ(deps2->getChildren()[0], stream0);
}

TEST(Resolver, PlatformPolicyManagerDirect) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  PlatformPolicyManager pm;
  bool ok = pm.loadPlatform(strideroot, "Pico");
  ASSERT_TRUE(ok);
  EXPECT_EQ(pm.getPlatformName(), "Pico");
  EXPECT_EQ(pm.getTargetDevice(), "RP2040");
  ASSERT_EQ(pm.getSupportedDevices().size(), 2u);
  EXPECT_EQ(pm.getSupportedDevices()[0], "RP2040");
  EXPECT_EQ(pm.getSupportedDevices()[1], "RP2350");

  // Verify resources for RP2040
  const auto &res2040 = pm.getHardwareResources();
  EXPECT_NE(std::find(res2040.begin(), res2040.end(), "PicoBootromFloat"), res2040.end());
  EXPECT_NE(std::find(res2040.begin(), res2040.end(), "PicoHardwareInterpolator"), res2040.end());

  // Policy resolution on RP2040
  std::string tag;
  EXPECT_EQ(pm.resolveVariant("Optimal", "Sin", &tag), "Sin@PicoRom");
  EXPECT_EQ(tag, "PicoRom");
  EXPECT_EQ(pm.resolveVariant("QualityPolicy::Fast", "Sin", &tag), "Sin@Poly5");
  EXPECT_EQ(tag, "Poly5");
  EXPECT_EQ(pm.resolveVariant("Accurate", "Sin", &tag), "Sin@Direct");
  EXPECT_EQ(tag, "Direct");
  EXPECT_EQ(pm.resolveVariant("Compact", "Sin", &tag), "Sin@PicoRom");
  EXPECT_EQ(tag, "PicoRom");

  // Overlay device RP2350
  PlatformPolicyManager pm2350;
  ok = pm2350.loadPlatform(strideroot, "Pico", "RP2350");
  ASSERT_TRUE(ok);
  EXPECT_EQ(pm2350.getTargetDevice(), "RP2350");
  const auto &res2350 = pm2350.getHardwareResources();
  // Cumulative resources
  EXPECT_NE(std::find(res2350.begin(), res2350.end(), "PicoBootromFloat"), res2350.end());
  EXPECT_NE(std::find(res2350.begin(), res2350.end(), "PicoHardwareInterpolator"), res2350.end());
  EXPECT_NE(std::find(res2350.begin(), res2350.end(), "CortexM33HardwareFpu"), res2350.end());

  // Policy resolution on RP2350: Optimal Sin and Cos are overridden to Direct!
  EXPECT_EQ(pm2350.resolveVariant("Optimal", "Sin", &tag), "Sin@Direct");
  EXPECT_EQ(tag, "Direct");
  EXPECT_EQ(pm2350.resolveVariant("Optimal", "Cos", &tag), "Cos@Direct");
  EXPECT_EQ(tag, "Direct");
  // Tan remains PicoRom from platform baseline
  EXPECT_EQ(pm2350.resolveVariant("Optimal", "Tan", &tag), "Tan@PicoRom");
  EXPECT_EQ(tag, "PicoRom");
  // Fast remains Poly5 from platform baseline
  EXPECT_EQ(pm2350.resolveVariant("Fast", "Sin", &tag), "Sin@Poly5");
  EXPECT_EQ(tag, "Poly5");
}

TEST(Resolver, PolicyVariantResolutionDefaultDevice) {
  std::string code = R"(
use Pico version 1.0

import QualityPolicy

_domainDefinition Audio {
    rate: 48000.0
    qualityPolicy: QualityPolicy::Optimal
}

signal SigX { domain: Audio }
signal SigY { domain: Audio }

SigX >> Sin() >> SigY;
)";
  auto fileName = writeCodeToFile(code);
  auto tree = AST::parseFile(fileName.c_str(), nullptr);
  ASSERT_NE(tree, nullptr);
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  CodeResolver resolver(tree, strideroot);
  resolver.process();

  EXPECT_EQ(resolver.getPolicyManager().getPlatformName(), "Pico");
  EXPECT_EQ(resolver.getPolicyManager().getTargetDevice(), "RP2040");

  bool foundSin = false;
  std::function<void(ASTNode)> checkNode = [&](ASTNode node) {
    if (!node) return;
    if (node->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(node);
      if (func->getName() == "Sin") {
        foundSin = true;
        auto atProp = func->getCompilerProperty("_at");
        ASSERT_NE(atProp, nullptr);
        EXPECT_EQ(std::static_pointer_cast<ValueNode>(atProp)->getStringValue(), "PicoRom");
        EXPECT_EQ(ASTQuery::getNodeName(func), "Sin@PicoRom");
      }
    }
    for (const auto &child : node->getChildren()) {
      checkNode(child);
    }
  };
  checkNode(tree);
  EXPECT_TRUE(foundSin);
}

TEST(Resolver, PolicyVariantResolutionDeviceOverlay) {
  std::string code = R"(
use Pico version 1.0 on RP2350

import QualityPolicy

_domainDefinition Audio {
    rate: 48000.0
    qualityPolicy: QualityPolicy::Optimal
}

signal SigX { domain: Audio }
signal SigY { domain: Audio }

SigX >> Sin() >> SigY;
)";
  auto fileName = writeCodeToFile(code);
  auto tree = AST::parseFile(fileName.c_str(), nullptr);
  ASSERT_NE(tree, nullptr);
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  CodeResolver resolver(tree, strideroot);
  resolver.process();

  EXPECT_EQ(resolver.getPolicyManager().getPlatformName(), "Pico");
  EXPECT_EQ(resolver.getPolicyManager().getTargetDevice(), "RP2350");

  bool foundSin = false;
  std::function<void(ASTNode)> checkNode = [&](ASTNode node) {
    if (!node) return;
    if (node->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(node);
      if (func->getName() == "Sin") {
        foundSin = true;
        auto atProp = func->getCompilerProperty("_at");
        ASSERT_NE(atProp, nullptr);
        EXPECT_EQ(std::static_pointer_cast<ValueNode>(atProp)->getStringValue(), "Direct");
        EXPECT_EQ(ASTQuery::getNodeName(func), "Sin@Direct");
      }
    }
    for (const auto &child : node->getChildren()) {
      checkNode(child);
    }
  };
  checkNode(tree);
  EXPECT_TRUE(foundSin);
}

TEST(Resolver, PolicyVariantSubdomainAndExplicitBypass) {
  std::string code = R"(
use Pico version 1.0 on RP2350

import QualityPolicy

_domainDefinition MainAudio {
    rate: 48000.0
    qualityPolicy: QualityPolicy::Optimal
}

_domainDefinition SubAudio {
    rate: 48000.0
    parentDomain: MainAudio
}

signal SigX { domain: SubAudio }
signal SigY { domain: SubAudio }
signal SigZ { domain: SubAudio }

SigX >> Sin() >> SigY;
SigX >> Sin@Poly5() >> SigZ;
)";
  auto fileName = writeCodeToFile(code);
  auto tree = AST::parseFile(fileName.c_str(), nullptr);
  ASSERT_NE(tree, nullptr);
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  CodeResolver resolver(tree, strideroot);
  resolver.process();

  int sinCount = 0;
  std::function<void(ASTNode)> checkNode = [&](ASTNode node) {
    if (!node) return;
    if (node->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(node);
      if (func->getName() == "Sin") {
        sinCount++;
        auto atProp = func->getCompilerProperty("_at");
        ASSERT_NE(atProp, nullptr);
        std::string tag = std::static_pointer_cast<ValueNode>(atProp)->getStringValue();
        EXPECT_TRUE(tag == "Direct" || tag == "Poly5");
      }
    }
    for (const auto &child : node->getChildren()) {
      checkNode(child);
    }
  };
  checkNode(tree);
  EXPECT_EQ(sinCount, 2);
}

