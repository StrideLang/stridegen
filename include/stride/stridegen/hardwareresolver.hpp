/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2026 Andres Cabrera
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
*/

#ifndef STRIDE_HARDWARERESOLVER_HPP
#define STRIDE_HARDWARERESOLVER_HPP

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace strd {

// The three layers of a hardware description. Each layer names the one
// below it with `overlays:`. Boards are declared as `hardwareBoard`, since
// `board` is a game library type:
//   board  -> board or device   (platforms/<P>/boards/<Name>.stride)
//   device -> device or core    (platforms/<P>/devices/<Name>.stride)
//   core   -> core              (cores/<Name>.stride, shared by platforms)
enum class HardwareKind { Core, Device, Board };

const char *hardwareKindName(HardwareKind kind);

struct HardwareLayer {
  HardwareKind kind;
  std::string name;
  std::string path;
  std::shared_ptr<DeclarationNode> declaration;
};

// One merged view of a board, device or core and everything it overlays.
//
// Merge rules, applied from the base layer up to the leaf:
//   - lists accumulate (an item already present is not added twice, and a
//     named entry such as `pin D13 { ... }` replaces the one below it);
//   - `{ key: value }` blocks merge key by key, recursively;
//   - any other value is replaced by the layer above.
// `overlays` itself is not merged.
class ResolvedHardware {
public:
  // Base layer first, the requested layer last.
  const std::vector<HardwareLayer> &chain() const { return m_chain; }
  const std::string &name() const;

  // The name of the top-most layer of a kind, or empty if the chain has
  // none: for an Uno, layerName(Device) is "ATmega328P".
  std::string layerName(HardwareKind kind) const;

  ASTNode property(const std::string &name) const;
  bool hasProperty(const std::string &name) const;
  std::string stringProperty(const std::string &name,
                             const std::string &defaultValue = "") const;
  std::optional<int64_t> intProperty(const std::string &name) const;
  // Names or strings of a list property. A single value reads as a list of
  // one.
  std::vector<std::string> nameListProperty(const std::string &name) const;

  // A value inside a `{ key: value }` block property, e.g.
  // blockValue("upload", "baud").
  ASTNode blockValue(const std::string &block, const std::string &key) const;
  std::string blockString(const std::string &block, const std::string &key,
                          const std::string &defaultValue = "") const;
  std::optional<int64_t> blockInt(const std::string &block,
                                  const std::string &key) const;
  // The keys of a block property, in declaration order.
  std::vector<std::string> blockKeys(const std::string &block) const;

  // A named entry of a list property, e.g. entry("pins", "D13") for
  // `pins: [ pin D13 { port: "B" bit: 5 } ]`, and values inside it.
  std::shared_ptr<DeclarationNode> entry(const std::string &list,
                                         const std::string &name) const;
  std::string entryString(const std::string &list, const std::string &name,
                          const std::string &key) const;
  std::optional<int64_t> entryInt(const std::string &list,
                                  const std::string &name,
                                  const std::string &key) const;

  const std::map<std::string, ASTNode> &properties() const {
    return m_properties;
  }

private:
  friend class HardwareResolver;
  std::vector<HardwareLayer> m_chain;
  std::map<std::string, ASTNode> m_properties;
};

class HardwareResolver {
public:
  explicit HardwareResolver(std::string strideroot);

  // Resolves `name` as a board, then a device of `platformName`, then a
  // shared core, and follows its overlay chain. Returns nothing and sets
  // `error` when a layer is missing, the chain has a cycle, or a layer
  // overlays a layer of a higher kind (a device overlaying a board).
  std::optional<ResolvedHardware> resolve(const std::string &platformName,
                                          const std::string &name,
                                          std::string *error = nullptr) const;

  // Finds the declaration of one layer without following its overlays.
  std::optional<HardwareLayer> findLayer(const std::string &platformName,
                                         const std::string &name) const;

  const std::string &strideroot() const { return m_strideroot; }

  // A string, entity name or literal as text. Empty for other nodes.
  static std::string valueText(const ASTNode &node);

private:
  std::string m_strideroot;
};

} // namespace strd

#endif // STRIDE_HARDWARERESOLVER_HPP
