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
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
// #include "stride/stridegen/stridelibrary.hpp"

using namespace strd;

TEST(CompilerProperties, ModuleInputPortBLock) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree;
  tree = AST::parseFile(TESTS_SOURCE_DIR "resolver/compiler_properties.stride");
  EXPECT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  for (const auto &node : tree->getChildren()) {
    if (node->getLine() == 23) {
      EXPECT_EQ(node->getNodeType(), AST::Stream);
      auto stream = std::static_pointer_cast<StreamNode>(node);
      auto funcNode = stream->getRight()->getChildren()[0];
      EXPECT_TRUE(funcNode);
      EXPECT_EQ(funcNode->getNodeType(), AST::Function);
      auto inputBlock = funcNode->getCompilerProperty("inputBlock");
      EXPECT_TRUE(inputBlock);
      EXPECT_EQ(inputBlock->getNodeType(), AST::Entity);
      EXPECT_EQ(std::static_pointer_cast<EntityNode>(inputBlock)->getName(),
                "OuterInput");
      auto outputBlock = funcNode->getCompilerProperty("outputBlock");
      EXPECT_TRUE(outputBlock);
      EXPECT_EQ(outputBlock->getNodeType(), AST::Entity);
      EXPECT_EQ(std::static_pointer_cast<EntityNode>(outputBlock)->getName(),
                "OuterOutput");
    }
  }
}
