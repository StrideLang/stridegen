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

    Author: Andres Cabrera
*/

#ifndef STRIDE_CPPCODEGEN_DOMAINCONNECTIONBUILDER_HPP
#define STRIDE_CPPCODEGEN_DOMAINCONNECTIONBUILDER_HPP

#include <map>
#include <string>

#include "stride/parser/ast.h"

namespace strd {
class StrideSystem;

struct ConnectionNodes {
  ASTNode sourceStreams;
  ASTNode sourceImports;
  ASTNode sourceEntities;
  ASTNode destStreams;
  ASTNode destImports;
  ASTNode destEntities;
};

// Generates C++ domain-crossing connection streams and entities.
class DomainConnectionBuilder {
public:
  static void generateDomainConnections(StrideSystem &system,
                                        const ASTNode &tree);

  static ConnectionNodes getDomainChangeStreams(StrideSystem &system,
                                                std::string previousDomainId,
                                                std::string nextDomainId,
                                                const ASTNode &tree = nullptr);

  static ASTNode
  substitutePropertiesInAST(ASTNode node,
                            const std::map<std::string, ASTNode> &props);
};
} // namespace strd

#endif // STRIDE_CPPCODEGEN_DOMAINCONNECTIONBUILDER_HPP
