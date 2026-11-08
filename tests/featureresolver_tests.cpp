/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

    Copyright (C) 2017-2026 Andres Cabrera
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

#include "gtest/gtest.h"

#include "stride/stridegen/featureresolver.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"

using namespace strd;

TEST(FeatureResolverTest, DefaultVariantSelected) {
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/default_and_choice.stride");
  ASSERT_TRUE(tree != nullptr);

  FeatureConfig config;
  FeatureResolver resolver(tree, config);
  ASSERT_TRUE(resolver.process());

  EXPECT_EQ(resolver.getActiveChoices().at("Pico"), "RT2030");
  EXPECT_TRUE(resolver.getActiveFeatures().count("Pico::RT2030") > 0);

  // BufferSize signal with default 256 should survive; RT2250 should be pruned
  int signalCount = 0;
  for (const auto &child : tree->getChildren()) {
    if (child && child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getName() == "BufferSize") {
        signalCount++;
        auto defProp = decl->getPropertyValue("default");
        ASSERT_TRUE(defProp != nullptr);
        ASSERT_EQ(defProp->getNodeType(), AST::Value);
        EXPECT_EQ(std::static_pointer_cast<ValueNode>(defProp)->toReal(), 256.0);
      }
    }
  }
  EXPECT_EQ(signalCount, 1);
}

TEST(FeatureResolverTest, ExplicitChoiceSelection) {
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/default_and_choice.stride");
  ASSERT_TRUE(tree != nullptr);

  FeatureConfig config;
  config.exclusiveChoices["Pico"] = "RT2250";
  config.enabledFeatures.insert("Pico::RT2250");
  config.enabledFeatures.insert("Pico");

  FeatureResolver resolver(tree, config);
  ASSERT_TRUE(resolver.process());

  EXPECT_EQ(resolver.getActiveChoices().at("Pico"), "RT2250");
  EXPECT_TRUE(resolver.getActiveFeatures().count("Pico::RT2250") > 0);

  // BufferSize signal with default 1024 should survive
  int signalCount = 0;
  for (const auto &child : tree->getChildren()) {
    if (child && child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getName() == "BufferSize") {
        signalCount++;
        auto defProp = decl->getPropertyValue("default");
        ASSERT_TRUE(defProp != nullptr);
        ASSERT_EQ(defProp->getNodeType(), AST::Value);
        EXPECT_EQ(std::static_pointer_cast<ValueNode>(defProp)->toReal(), 1024.0);
      }
    }
  }
  EXPECT_EQ(signalCount, 1);
}

TEST(FeatureResolverTest, NoDefaultFeatures) {
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/default_and_choice.stride");
  ASSERT_TRUE(tree != nullptr);

  FeatureConfig config;
  config.useDefaultFeatures = false;

  FeatureResolver resolver(tree, config);
  ASSERT_TRUE(resolver.process());

  // Neither variant was selected, so both @with signals should be pruned
  int signalCount = 0;
  for (const auto &child : tree->getChildren()) {
    if (child && child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getName() == "BufferSize") {
        signalCount++;
      }
    }
  }
  EXPECT_EQ(signalCount, 0);
}

TEST(FeatureResolverTest, BooleanLogicEvaluation) {
  // Case 1: HardwareSPI enabled, FastClock not enabled
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/boolean_logic.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    config.enabledFeatures.insert("HardwareSPI");

    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    int modeCount = 0;
    for (const auto &child : tree->getChildren()) {
      if (child && child->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(child);
        if (decl->getName() == "Mode") {
          modeCount++;
          auto val = std::static_pointer_cast<ValueNode>(decl->getPropertyValue("default"));
          EXPECT_EQ(val->toReal(), 1.0); // Mode 1
        }
      }
    }
    EXPECT_EQ(modeCount, 1);
  }

  // Case 2: HardwareSPI enabled, FastClock enabled
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/boolean_logic.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    config.enabledFeatures.insert("HardwareSPI");
    config.enabledFeatures.insert("FastClock");

    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    int modeCount = 0;
    for (const auto &child : tree->getChildren()) {
      if (child && child->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(child);
        if (decl->getName() == "Mode") {
          modeCount++;
          auto val = std::static_pointer_cast<ValueNode>(decl->getPropertyValue("default"));
          EXPECT_EQ(val->toReal(), 2.0); // Mode 2
        }
      }
    }
    EXPECT_EQ(modeCount, 1);
  }

  // Case 3: HardwareSPI not enabled
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/boolean_logic.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config; // Nothing enabled

    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    int modeCount = 0;
    for (const auto &child : tree->getChildren()) {
      if (child && child->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(child);
        if (decl->getName() == "Mode") {
          modeCount++;
          auto val = std::static_pointer_cast<ValueNode>(decl->getPropertyValue("default"));
          EXPECT_EQ(val->toReal(), 3.0); // Mode 3
        }
      }
    }
    EXPECT_EQ(modeCount, 1);
  }
}

TEST(FeatureResolverTest, BlockGroupPruning) {
  // When Audio disabled: Volume and Mute pruned, AlwaysPresent remains
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/block_group.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    bool foundAlwaysPresent = false;
    for (const auto &child : tree->getChildren()) {
      if (child && child->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(child);
        EXPECT_NE(decl->getName(), "Volume");
        EXPECT_NE(decl->getName(), "Mute");
        if (decl->getName() == "AlwaysPresent") {
          foundAlwaysPresent = true;
        }
      }
    }
    EXPECT_TRUE(foundAlwaysPresent);
  }

  // When Audio enabled: Volume, Mute, and AlwaysPresent all remain
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/block_group.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    config.enabledFeatures.insert("Audio");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    int declCount = 0;
    for (const auto &child : tree->getChildren()) {
      if (child && child->getNodeType() == AST::Declaration) {
        declCount++;
      }
    }
    EXPECT_EQ(declCount, 3);
  }
}

TEST(FeatureResolverTest, ConditionalPorts) {
  // Case 1: ExtraInput disabled -> processor inputs has 1 port
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/conditional_ports.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    auto mod = std::static_pointer_cast<DeclarationNode>(tree->getChildren().at(0));
    auto inputsVal = std::static_pointer_cast<ListNode>(mod->getPropertyValue("inputs"));
    ASSERT_EQ(inputsVal->getChildren().size(), 1);
    EXPECT_EQ(ASTQuery::getNodeName(inputsVal->getChildren().at(0)), "In1");
  }

  // Case 2: ExtraInput enabled -> processor inputs has 2 ports
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/conditional_ports.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    config.enabledFeatures.insert("ExtraInput");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    auto mod = std::static_pointer_cast<DeclarationNode>(tree->getChildren().at(0));
    auto inputsVal = std::static_pointer_cast<ListNode>(mod->getPropertyValue("inputs"));
    ASSERT_EQ(inputsVal->getChildren().size(), 2);
    EXPECT_EQ(ASTQuery::getNodeName(inputsVal->getChildren().at(0)), "In1");
    EXPECT_EQ(ASTQuery::getNodeName(inputsVal->getChildren().at(1)), "In2");
  }
}

TEST(FeatureResolverTest, InlineConditionalPropertyResolution) {
  // Default Target is LowEnd -> FifoDepth default is 16
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/conditional_properties.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    auto sig = std::static_pointer_cast<DeclarationNode>(tree->getChildren().at(0));
    auto defVal = std::static_pointer_cast<ValueNode>(sig->getPropertyValue("default"));
    EXPECT_EQ(defVal->toReal(), 16.0);
  }

  // Target HighEnd -> FifoDepth default is 128
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/conditional_properties.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    config.exclusiveChoices["Target"] = "HighEnd";
    config.enabledFeatures.insert("Target::HighEnd");
    config.enabledFeatures.insert("Target");

    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    auto sig = std::static_pointer_cast<DeclarationNode>(tree->getChildren().at(0));
    auto defVal = std::static_pointer_cast<ValueNode>(sig->getPropertyValue("default"));
    EXPECT_EQ(defVal->toReal(), 128.0);
  }
}

TEST(FeatureResolverTest, NonExhaustivePropertyError) {
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/non_exhaustive_prop.stride");
  ASSERT_TRUE(tree != nullptr);

  FeatureConfig config; // Target defaults to LowEnd, but property only specifies HighEnd
  FeatureResolver resolver(tree, config);
  EXPECT_FALSE(resolver.process());

  ASSERT_FALSE(resolver.getErrors().empty());
  EXPECT_EQ(resolver.getErrors().at(0).type, LangError::NonExhaustiveProperty);
}

TEST(FeatureResolverTest, AmbiguousPropertyError) {
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/ambiguous_prop.stride");
  ASSERT_TRUE(tree != nullptr);

  FeatureConfig config;
  config.enabledFeatures.insert("FastClock");
  config.enabledFeatures.insert("HighPrecision");
  FeatureResolver resolver(tree, config);
  EXPECT_FALSE(resolver.process());

  ASSERT_FALSE(resolver.getErrors().empty());
  EXPECT_EQ(resolver.getErrors().at(0).type, LangError::AmbiguousProperty);
}

TEST(FeatureResolverTest, UndeclaredFeatureError) {
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/undeclared_feature.stride");
  ASSERT_TRUE(tree != nullptr);

  FeatureConfig config;
  FeatureResolver resolver(tree, config);
  EXPECT_FALSE(resolver.process());

  ASSERT_FALSE(resolver.getErrors().empty());
  EXPECT_EQ(resolver.getErrors().at(0).type, LangError::UndeclaredFeature);
}

TEST(FeatureResolverTest, InvalidVariantError) {
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/invalid_variant.stride");
  ASSERT_TRUE(tree != nullptr);

  FeatureConfig config;
  FeatureResolver resolver(tree, config);
  EXPECT_FALSE(resolver.process());

  ASSERT_FALSE(resolver.getErrors().empty());
  EXPECT_EQ(resolver.getErrors().at(0).type, LangError::InvalidFeatureVariant);
}

TEST(FeatureResolverTest, FileLocalForceDirective) {
  // Case 1: In-source @force(Pico::RT2250) overrides default RT2030
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/force_directive.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    // Selected signal should have default 20 (RT2250)
    int signalCount = 0;
    for (const auto &child : tree->getChildren()) {
      if (child && child->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(child);
        if (decl->getName() == "Selected") {
          signalCount++;
          auto val = std::static_pointer_cast<ValueNode>(decl->getPropertyValue("default"));
          EXPECT_EQ(val->toReal(), 20.0);
        }
      }
    }
    EXPECT_EQ(signalCount, 1);
  }

  // Case 2: Command-line flag overrides in-source @force
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/force_directive.stride");
    ASSERT_TRUE(tree != nullptr);

    FeatureConfig config;
    config.exclusiveChoices["Pico"] = "RT2030";
    config.enabledFeatures.insert("Pico::RT2030");
    config.enabledFeatures.insert("Pico");

    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());

    // CLI forced RT2030, so Selected signal should have default 10 (RT2030)
    int signalCount = 0;
    for (const auto &child : tree->getChildren()) {
      if (child && child->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(child);
        if (decl->getName() == "Selected") {
          signalCount++;
          auto val = std::static_pointer_cast<ValueNode>(decl->getPropertyValue("default"));
          EXPECT_EQ(val->toReal(), 10.0);
        }
      }
    }
    EXPECT_EQ(signalCount, 1);
  }
}

TEST(FeatureResolverTest, ComplexPrecedenceAndParentheses) {
  // Case 1: A=true, B=false, C=true
  // (A || B) && !C => (T || F) && F => false (Grouped pruned)
  // A || (B && !C) => T || (F && F) => true  (Precedence survives)
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/complex_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.enabledFeatures.insert("FeatureA");
    config.enabledFeatures.insert("FeatureC");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    bool foundGrouped = false;
    bool foundPrecedence = false;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) {
        auto d = std::static_pointer_cast<DeclarationNode>(c);
        if (d->getName() == "Grouped") foundGrouped = true;
        if (d->getName() == "Precedence") foundPrecedence = true;
      }
    }
    EXPECT_FALSE(foundGrouped);
    EXPECT_TRUE(foundPrecedence);
  }

  // Case 2: A=false, B=true, C=false
  // (A || B) && !C => (F || T) && T => true (Grouped survives)
  // A || (B && !C) => F || (T && T) => true (Precedence survives)
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/complex_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.enabledFeatures.insert("FeatureB");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    bool foundGrouped = false;
    bool foundPrecedence = false;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) {
        auto d = std::static_pointer_cast<DeclarationNode>(c);
        if (d->getName() == "Grouped") foundGrouped = true;
        if (d->getName() == "Precedence") foundPrecedence = true;
      }
    }
    EXPECT_TRUE(foundGrouped);
    EXPECT_TRUE(foundPrecedence);
  }

  // Case 3: A=false, B=true, C=true
  // Both evaluate to false (both pruned)
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/complex_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.enabledFeatures.insert("FeatureB");
    config.enabledFeatures.insert("FeatureC");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    int declCount = 0;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) declCount++;
    }
    EXPECT_EQ(declCount, 0);
  }
}

TEST(FeatureResolverTest, ScopedVariantInCompoundExpressions) {
  // Case 1: Default RT2030 -> BranchA survives, BranchB pruned
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/scoped_variant_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    bool foundA = false;
    bool foundB = false;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) {
        auto d = std::static_pointer_cast<DeclarationNode>(c);
        if (d->getName() == "BranchA") foundA = true;
        if (d->getName() == "BranchB") foundB = true;
      }
    }
    EXPECT_TRUE(foundA);
    EXPECT_FALSE(foundB);
  }

  // Case 2: RT2250 + FastClock -> BranchA survives, BranchB pruned
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/scoped_variant_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.exclusiveChoices["Pico"] = "RT2250";
    config.enabledFeatures.insert("Pico::RT2250");
    config.enabledFeatures.insert("Pico");
    config.enabledFeatures.insert("FastClock");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    bool foundA = false;
    bool foundB = false;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) {
        auto d = std::static_pointer_cast<DeclarationNode>(c);
        if (d->getName() == "BranchA") foundA = true;
        if (d->getName() == "BranchB") foundB = true;
      }
    }
    EXPECT_TRUE(foundA);
    EXPECT_FALSE(foundB);
  }

  // Case 3: RT2250 without FastClock -> BranchA pruned, BranchB survives
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/scoped_variant_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.exclusiveChoices["Pico"] = "RT2250";
    config.enabledFeatures.insert("Pico::RT2250");
    config.enabledFeatures.insert("Pico");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    bool foundA = false;
    bool foundB = false;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) {
        auto d = std::static_pointer_cast<DeclarationNode>(c);
        if (d->getName() == "BranchA") foundA = true;
        if (d->getName() == "BranchB") foundB = true;
      }
    }
    EXPECT_FALSE(foundA);
    EXPECT_TRUE(foundB);
  }
}

TEST(FeatureResolverTest, StreamGuardedByFeatureExpressions) {
  // Case 1: Logging && !Detailed
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/stream_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.enabledFeatures.insert("Logging");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    int streamCount = 0;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Stream) streamCount++;
    }
    EXPECT_EQ(streamCount, 1);
  }

  // Case 2: Logging && Detailed
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/stream_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.enabledFeatures.insert("Logging");
    config.enabledFeatures.insert("Detailed");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    int streamCount = 0;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Stream) streamCount++;
    }
    EXPECT_EQ(streamCount, 1);
  }

  // Case 3: !Logging
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/stream_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config; // Logging disabled
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    int streamCount = 0;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Stream) streamCount++;
    }
    EXPECT_EQ(streamCount, 1);
  }
}

TEST(FeatureResolverTest, NestedBlockGroupExpressions) {
  // Case 1: Outer group condition false (neither A nor B) -> all pruned
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/nested_block_group_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config; // Nothing enabled
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    int declCount = 0;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) declCount++;
    }
    EXPECT_EQ(declCount, 0);
  }

  // Case 2: FeatureA + FeatureC enabled -> AlwaysInGroup and InnerWithC survive
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/nested_block_group_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.enabledFeatures.insert("FeatureA");
    config.enabledFeatures.insert("FeatureC");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    bool foundAlways = false;
    bool foundWithC = false;
    bool foundWithoutC = false;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) {
        auto d = std::static_pointer_cast<DeclarationNode>(c);
        if (d->getName() == "AlwaysInGroup") foundAlways = true;
        if (d->getName() == "InnerWithC") foundWithC = true;
        if (d->getName() == "InnerWithoutC") foundWithoutC = true;
      }
    }
    EXPECT_TRUE(foundAlways);
    EXPECT_TRUE(foundWithC);
    EXPECT_FALSE(foundWithoutC);
  }

  // Case 3: FeatureB enabled, FeatureC disabled -> AlwaysInGroup and InnerWithoutC survive
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/nested_block_group_expressions.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.enabledFeatures.insert("FeatureB");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    bool foundAlways = false;
    bool foundWithC = false;
    bool foundWithoutC = false;
    for (const auto &c : tree->getChildren()) {
      if (c && c->getNodeType() == AST::Declaration) {
        auto d = std::static_pointer_cast<DeclarationNode>(c);
        if (d->getName() == "AlwaysInGroup") foundAlways = true;
        if (d->getName() == "InnerWithC") foundWithC = true;
        if (d->getName() == "InnerWithoutC") foundWithoutC = true;
      }
    }
    EXPECT_TRUE(foundAlways);
    EXPECT_FALSE(foundWithC);
    EXPECT_TRUE(foundWithoutC);
  }
}

TEST(FeatureResolverTest, ConditionalPropertyWithExpressionValues) {
  // Case 1: FastClock enabled -> Buffer default is (1024 * 2)
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/conditional_property_expression_values.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    config.enabledFeatures.insert("FastClock");
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    auto sig = std::static_pointer_cast<DeclarationNode>(tree->getChildren().at(0));
    auto defVal = sig->getPropertyValue("default");
    ASSERT_TRUE(defVal != nullptr);
    ASSERT_EQ(defVal->getNodeType(), AST::Expression);
    auto expr = std::static_pointer_cast<ExpressionNode>(defVal);
    EXPECT_EQ(expr->getExpressionType(), ExpressionNode::Multiply);
  }

  // Case 2: FastClock disabled -> Buffer default is (256 / 2)
  {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "features/conditional_property_expression_values.stride");
    ASSERT_TRUE(tree != nullptr);
    FeatureConfig config;
    FeatureResolver resolver(tree, config);
    ASSERT_TRUE(resolver.process());
    auto sig = std::static_pointer_cast<DeclarationNode>(tree->getChildren().at(0));
    auto defVal = sig->getPropertyValue("default");
    ASSERT_TRUE(defVal != nullptr);
    ASSERT_EQ(defVal->getNodeType(), AST::Expression);
    auto expr = std::static_pointer_cast<ExpressionNode>(defVal);
    EXPECT_EQ(expr->getExpressionType(), ExpressionNode::Divide);
  }
}
