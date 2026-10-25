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

  auto constDecl = ASTQuery::findDeclarationByName("RangeConst", ScopeStack(), tree);
  ASSERT_NE(constDecl, nullptr);
  auto constVal = constDecl->getPropertyValue("value");
  ASSERT_NE(constVal, nullptr);
  ASSERT_EQ(constVal->getNodeType(), AST::List);
  auto constList = std::static_pointer_cast<ListNode>(constVal);
  ASSERT_EQ(constList->getChildren().size(), 1);
  ASSERT_EQ(constList->getChildren().at(0)->getNodeType(), AST::GeneratorRange);
  auto constGen = std::static_pointer_cast<GeneratorRangeNode>(constList->getChildren().at(0));
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(constGen->startIndex())->getIntValue(), 0);
  EXPECT_EQ(std::static_pointer_cast<ValueNode>(constGen->endIndex())->getIntValue(), 8);

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

  // Ranges in a default are expanded so there is one value per element.
  auto expectDefault = [](std::shared_ptr<DeclarationNode> decl,
                          const std::vector<int64_t> &expected) {
    ASSERT_NE(decl, nullptr);
    auto sigDefault = decl->getPropertyValue("default");
    ASSERT_NE(sigDefault, nullptr);
    ASSERT_EQ(sigDefault->getNodeType(), AST::List);
    auto children = sigDefault->getChildren();
    ASSERT_EQ(children.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
      ASSERT_EQ(children.at(i)->getNodeType(), AST::Int);
      EXPECT_EQ(std::static_pointer_cast<ValueNode>(children.at(i))->getIntValue(),
                expected[i]);
    }
  };
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
