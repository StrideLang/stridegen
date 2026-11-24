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

#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>

namespace strd {

const char *hardwareKindName(HardwareKind kind) {
  switch (kind) {
  case HardwareKind::Core:
    return "core";
  case HardwareKind::Device:
    return "device";
  case HardwareKind::Board:
    // Not "board": the game library already has a `board` type.
    return "hardwareBoard";
  }
  return "";
}

namespace {

std::shared_ptr<DeclarationNode> asBlock(const ASTNode &node) {
  if (!node || node->getNodeType() != AST::Declaration) {
    return nullptr;
  }
  auto decl = std::static_pointer_cast<DeclarationNode>(node);
  if (!decl->getName().empty() || !decl->getEntityType().empty()) {
    return nullptr; // a named declaration is a value, not a block
  }
  return decl;
}

ASTNode mergeValues(const ASTNode &base, const ASTNode &over);

// Items of the layer above are added after the base's. An item whose name or
// text is already there is not added twice, except a named entry
// (`pin D13 { ... }`), which replaces the base's entry of the same name.
ASTNode mergeLists(const ASTNode &base, const ASTNode &over) {
  auto merged = base->deepCopy();
  auto items = merged->getChildren();
  for (const auto &item : over->getChildren()) {
    auto text = HardwareResolver::valueText(item);
    auto existing = text.empty()
                        ? items.end()
                        : std::find_if(items.begin(), items.end(),
                                       [&](const ASTNode &candidate) {
                                         return HardwareResolver::valueText(
                                                    candidate) == text;
                                       });
    if (existing == items.end()) {
      items.push_back(item->deepCopy());
    } else if (item->getNodeType() == AST::Declaration) {
      *existing = item->deepCopy();
    }
  }
  merged->setChildren(items);
  return merged;
}

ASTNode mergeBlocks(const std::shared_ptr<DeclarationNode> &base,
                    const std::shared_ptr<DeclarationNode> &over) {
  auto merged = std::static_pointer_cast<DeclarationNode>(base->deepCopy());
  for (const auto &prop : over->getProperties()) {
    auto existing = merged->getPropertyValue(prop->getName());
    if (existing) {
      merged->replacePropertyValue(prop->getName(),
                                   mergeValues(existing, prop->getValue()));
    } else {
      merged->addProperty(
          std::static_pointer_cast<PropertyNode>(prop->deepCopy()));
    }
  }
  return merged;
}

ASTNode mergeValues(const ASTNode &base, const ASTNode &over) {
  if (!base) {
    return over ? over->deepCopy() : nullptr;
  }
  if (!over) {
    return base;
  }
  if (base->getNodeType() == AST::List && over->getNodeType() == AST::List) {
    return mergeLists(base, over);
  }
  auto baseBlock = asBlock(base);
  auto overBlock = asBlock(over);
  if (baseBlock && overBlock) {
    return mergeBlocks(baseBlock, overBlock);
  }
  return over->deepCopy();
}

std::shared_ptr<DeclarationNode> findDeclaration(const std::string &path,
                                                 const std::string &entityType,
                                                 const std::string &name) {
  if (!std::filesystem::exists(path)) {
    return nullptr;
  }
  auto tree = AST::parseFile(path.c_str(), nullptr);
  if (!tree) {
    return nullptr;
  }
  for (const auto &child : tree->getChildren()) {
    if (child->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(child);
    if (decl->getEntityType() == entityType && decl->getName() == name) {
      return decl;
    }
  }
  return nullptr;
}

} // namespace

// ResolvedHardware -----------------------------------------------------------

const std::string &ResolvedHardware::name() const {
  static const std::string empty;
  return m_chain.empty() ? empty : m_chain.back().name;
}

std::string ResolvedHardware::layerName(HardwareKind kind) const {
  for (auto it = m_chain.rbegin(); it != m_chain.rend(); ++it) {
    if (it->kind == kind) {
      return it->name;
    }
  }
  return {};
}

ASTNode ResolvedHardware::property(const std::string &name) const {
  auto it = m_properties.find(name);
  return it == m_properties.end() ? nullptr : it->second;
}

bool ResolvedHardware::hasProperty(const std::string &name) const {
  auto value = property(name);
  return value && value->getNodeType() != AST::None;
}

std::string
ResolvedHardware::stringProperty(const std::string &name,
                                 const std::string &defaultValue) const {
  auto text = HardwareResolver::valueText(property(name));
  return text.empty() ? defaultValue : text;
}

static std::optional<int64_t> intOf(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Int) {
    return std::static_pointer_cast<ValueNode>(node)->getIntValue();
  }
  return std::nullopt;
}

std::optional<int64_t>
ResolvedHardware::intProperty(const std::string &name) const {
  return intOf(property(name));
}

std::vector<std::string>
ResolvedHardware::nameListProperty(const std::string &name) const {
  std::vector<std::string> names;
  auto value = property(name);
  if (!value || value->getNodeType() == AST::None) {
    return names;
  }
  if (value->getNodeType() == AST::List) {
    for (const auto &item : value->getChildren()) {
      auto text = HardwareResolver::valueText(item);
      if (!text.empty()) {
        names.push_back(text);
      }
    }
  } else {
    auto text = HardwareResolver::valueText(value);
    if (!text.empty()) {
      names.push_back(text);
    }
  }
  return names;
}

ASTNode ResolvedHardware::blockValue(const std::string &block,
                                     const std::string &key) const {
  auto decl = asBlock(property(block));
  return decl ? decl->getPropertyValue(key) : nullptr;
}

std::string
ResolvedHardware::blockString(const std::string &block, const std::string &key,
                              const std::string &defaultValue) const {
  auto text = HardwareResolver::valueText(blockValue(block, key));
  return text.empty() ? defaultValue : text;
}

std::optional<int64_t> ResolvedHardware::blockInt(const std::string &block,
                                                  const std::string &key) const {
  return intOf(blockValue(block, key));
}

std::vector<std::string>
ResolvedHardware::blockKeys(const std::string &block) const {
  std::vector<std::string> keys;
  if (auto decl = asBlock(property(block))) {
    for (const auto &prop : decl->getProperties()) {
      keys.push_back(prop->getName());
    }
  }
  return keys;
}

std::shared_ptr<DeclarationNode>
ResolvedHardware::entry(const std::string &list,
                        const std::string &name) const {
  auto value = property(list);
  if (!value || value->getNodeType() != AST::List) {
    return nullptr;
  }
  for (const auto &item : value->getChildren()) {
    if (item->getNodeType() == AST::Declaration &&
        std::static_pointer_cast<DeclarationNode>(item)->getName() == name) {
      return std::static_pointer_cast<DeclarationNode>(item);
    }
  }
  return nullptr;
}

std::string ResolvedHardware::entryString(const std::string &list,
                                          const std::string &name,
                                          const std::string &key) const {
  auto decl = entry(list, name);
  return decl ? HardwareResolver::valueText(decl->getPropertyValue(key))
              : std::string();
}

std::optional<int64_t> ResolvedHardware::entryInt(const std::string &list,
                                                  const std::string &name,
                                                  const std::string &key) const {
  auto decl = entry(list, name);
  return decl ? intOf(decl->getPropertyValue(key)) : std::nullopt;
}

// HardwareResolver -----------------------------------------------------------

HardwareResolver::HardwareResolver(std::string strideroot)
    : m_strideroot(std::move(strideroot)) {}

std::string HardwareResolver::valueText(const ASTNode &node) {
  if (!node) {
    return {};
  }
  switch (node->getNodeType()) {
  case AST::String:
    return std::static_pointer_cast<ValueNode>(node)->getStringValue();
  case AST::Int:
    return std::to_string(
        std::static_pointer_cast<ValueNode>(node)->getIntValue());
  case AST::Real:
  case AST::Switch:
    return std::static_pointer_cast<ValueNode>(node)->toString();
  case AST::Declaration:
    return std::static_pointer_cast<DeclarationNode>(node)->getName();
  case AST::Entity:
  case AST::Array:
  case AST::Function:
    return ASTQuery::getNodeName(node);
  default:
    return {};
  }
}

std::optional<HardwareLayer>
HardwareResolver::findLayer(const std::string &platformName,
                            const std::string &name) const {
  struct Candidate {
    HardwareKind kind;
    std::string path;
  };
  std::vector<Candidate> candidates;
  // A project's own layers, next to its program, come first. Any kind may be
  // declared there: a board over a stock board, or a device for an FPGA soft
  // core's GPIO block.
  for (const auto &dir : m_searchDirectories) {
    for (auto kind : {HardwareKind::Board, HardwareKind::Device, HardwareKind::Core}) {
      candidates.push_back({kind, dir + "/" + name + ".stride"});
    }
  }
  if (!platformName.empty()) {
    auto platformDir = m_strideroot + "/platforms/" + platformName;
    candidates.push_back(
        {HardwareKind::Board, platformDir + "/boards/" + name + ".stride"});
    candidates.push_back(
        {HardwareKind::Device, platformDir + "/devices/" + name + ".stride"});
    // A core only this platform's devices use, such as AVR5.
    candidates.push_back(
        {HardwareKind::Core, platformDir + "/cores/" + name + ".stride"});
  }
  // A core several platforms share.
  candidates.push_back(
      {HardwareKind::Core, m_strideroot + "/cores/" + name + ".stride"});

  for (const auto &candidate : candidates) {
    auto decl = findDeclaration(candidate.path,
                                hardwareKindName(candidate.kind), name);
    if (decl) {
      return HardwareLayer{candidate.kind, name, candidate.path, decl};
    }
  }
  return std::nullopt;
}

std::optional<ResolvedHardware>
HardwareResolver::resolve(const std::string &platformName,
                          const std::string &name, std::string *error) const {
  auto fail = [&](const std::string &message) -> std::optional<ResolvedHardware> {
    if (error) {
      *error = message;
    }
    return std::nullopt;
  };

  ResolvedHardware resolved;
  std::set<std::string> visited;
  std::string current = name;
  while (!current.empty()) {
    if (visited.count(current)) {
      return fail("hardware overlay cycle at '" + current + "'");
    }
    visited.insert(current);

    auto layer = findLayer(platformName, current);
    if (!layer) {
      std::string message = "unknown board, device or core '" + current + "'";
      if (!resolved.m_chain.empty()) {
        message += " (overlaid by " + std::string(hardwareKindName(
                                          resolved.m_chain.back().kind)) +
                   " '" + resolved.m_chain.back().name + "')";
      }
      return fail(message);
    }
    if (!resolved.m_chain.empty() &&
        static_cast<int>(layer->kind) >
            static_cast<int>(resolved.m_chain.back().kind)) {
      return fail(std::string(hardwareKindName(resolved.m_chain.back().kind)) +
                  " '" + resolved.m_chain.back().name + "' cannot overlay " +
                  hardwareKindName(layer->kind) + " '" + current + "'");
    }
    resolved.m_chain.push_back(*layer);
    current = valueText(layer->declaration->getPropertyValue("overlays"));
  }

  // Leaf first while walking; store and merge base first.
  std::reverse(resolved.m_chain.begin(), resolved.m_chain.end());
  for (const auto &layer : resolved.m_chain) {
    for (const auto &prop : layer.declaration->getProperties()) {
      if (prop->getName() == "overlays") {
        continue;
      }
      auto &slot = resolved.m_properties[prop->getName()];
      slot = mergeValues(slot, prop->getValue());
    }
  }
  return resolved;
}

// GPIO -----------------------------------------------------------------------

namespace {

// The name an option value was written with: Gpio::PullUp gives PullUp, and a
// value folded from a constant gives the constant's name.
std::string optionName(const ASTNode &node) {
  if (!node || node->getNodeType() == AST::None) {
    return {};
  }
  if (auto origin = node->getCompilerProperty("resolvedFrom")) {
    if (origin->getNodeType() == AST::Declaration) {
      return std::static_pointer_cast<DeclarationNode>(origin)->getName();
    }
  }
  return HardwareResolver::valueText(node);
}

std::optional<int64_t> intValue(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Int) {
    return std::static_pointer_cast<ValueNode>(node)->getIntValue();
  }
  return std::nullopt;
}

std::optional<bool> switchValue(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Switch) {
    return std::static_pointer_cast<ValueNode>(node)->getSwitchValue();
  }
  return std::nullopt;
}

std::vector<ASTNode> listItems(const ASTNode &node) {
  if (node && node->getNodeType() == AST::List) {
    return node->getChildren();
  }
  return {};
}

std::vector<std::string> nameList(const ASTNode &node) {
  std::vector<std::string> names;
  for (const auto &item : listItems(node)) {
    auto name = optionName(item);
    if (!name.empty()) {
      names.push_back(name);
    }
  }
  return names;
}

// A `key: value` of a block written either untyped ({ ... }) or as an
// anonymous typed block (gpioCapabilities { ... }).
ASTNode anonymousBlockValue(const ASTNode &block, const std::string &key) {
  if (!block || block->getNodeType() != AST::Declaration) {
    return nullptr;
  }
  auto decl = std::static_pointer_cast<DeclarationNode>(block);
  // The parser names anonymous blocks ___Anonymous___<n>.
  const auto &name = decl->getName();
  if (!name.empty() && name.rfind("___Anonymous___", 0) != 0) {
    return nullptr;
  }
  return decl->getPropertyValue(key);
}

std::shared_ptr<DeclarationNode> asDeclaration(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Declaration) {
    return std::static_pointer_cast<DeclarationNode>(node);
  }
  return nullptr;
}

PinRoute routeFrom(const std::shared_ptr<DeclarationNode> &decl) {
  PinRoute route;
  route.function = optionName(decl->getPropertyValue("function"));
  route.select = intValue(decl->getPropertyValue("select"));
  route.reg = intValue(decl->getPropertyValue("register"));
  return route;
}

// The typeClass each gpioPin key takes its values from.
const std::vector<std::pair<std::string, std::string>> &optionKeys() {
  static const std::vector<std::pair<std::string, std::string>> keys = {
      {"mode", "_PinModes"},       {"outputType", "_PinOutputTypes"},
      {"pull", "_PinPulls"},       {"drive", "_PinDrives"},
      {"slew", "_PinSlews"},       {"interrupt", "_PinInterrupts"},
      {"ioStandard", "_PinIoStandards"}};
  return keys;
}

// Values every pin supports without a field: the reset state.
bool isDefaultValue(const std::string &value) {
  static const std::set<std::string> defaults = {
      "OutputPushPull", "PullNone",      "DriveDefault",
      "SlewDefault",    "InterruptNone", "IoDefault"};
  return defaults.count(value) > 0;
}

std::string valueOf(const GpioPinConfig &pin, const std::string &key) {
  if (key == "mode") return pin.mode;
  if (key == "outputType") return pin.outputType;
  if (key == "pull") return pin.pull;
  if (key == "drive") return pin.drive;
  if (key == "slew") return pin.slew;
  if (key == "interrupt") return pin.interrupt;
  if (key == "ioStandard") return pin.ioStandard;
  return {};
}

std::string joined(const std::vector<std::string> &items) {
  std::string text;
  for (const auto &item : items) {
    text += (text.empty() ? "" : ", ") + item;
  }
  return text.empty() ? "none" : text;
}

bool contains(const std::vector<std::string> &items, const std::string &item) {
  return std::find(items.begin(), items.end(), item) != items.end();
}

// Address and bit offset of a field for one pin bit.
std::pair<int64_t, int> fieldLocation(const GpioField &field, int bit,
                                      int width) {
  if (field.pinStride > 0) {
    return {field.reg + field.pinStride * bit, field.shift};
  }
  int offset = bit * field.bitsPerPin + field.shift;
  if (offset >= width && field.regHigh) {
    return {*field.regHigh, offset - width};
  }
  return {field.reg, offset};
}

uint64_t fieldMask(const GpioField &field) {
  return field.bitsPerPin >= 64 ? ~0ull : ((1ull << field.bitsPerPin) - 1);
}

bool isOutput(const GpioPinConfig &pin) { return pin.mode == "PinOutput"; }

} // namespace

std::optional<int64_t> GpioField::codeFor(const std::string &value) const {
  for (size_t i = 0; i < options.size() && i < codes.size(); ++i) {
    if (options[i] == value) {
      return codes[i];
    }
  }
  return std::nullopt;
}

GpioPinConfig
GpioPinConfig::fromDeclaration(const std::shared_ptr<DeclarationNode> &decl) {
  GpioPinConfig pin;
  pin.name = decl->getName();
  pin.file = decl->getFilename();
  pin.line = decl->getLine();
  if (auto label = decl->getPropertyValue("pin")) {
    pin.label = HardwareResolver::valueText(label);
    auto ns = label->getNamespaceList();
    if (!ns.empty()) {
      pin.boardQualifier = ns.back();
    }
  }
  auto text = [&](const char *key, std::string &target) {
    auto value = optionName(decl->getPropertyValue(key));
    if (!value.empty() && value != "none") {
      target = value;
    }
  };
  text("mode", pin.mode);
  text("outputType", pin.outputType);
  text("pull", pin.pull);
  text("drive", pin.drive);
  text("slew", pin.slew);
  text("interrupt", pin.interrupt);
  text("ioStandard", pin.ioStandard);
  text("function", pin.function);
  pin.hysteresis = switchValue(decl->getPropertyValue("hysteresis"));
  pin.activeLow = switchValue(decl->getPropertyValue("activeLow")).value_or(false);
  pin.initial = switchValue(decl->getPropertyValue("initial")).value_or(false);
  return pin;
}

bool ResolvedHardware::hasLayer(const std::string &name) const {
  return std::any_of(m_chain.begin(), m_chain.end(),
                     [&](const HardwareLayer &layer) { return layer.name == name; });
}

std::vector<GpioPort> ResolvedHardware::gpioPorts() const {
  std::vector<GpioPort> ports;
  int index = 0;
  for (const auto &item : listItems(property("ports"))) {
    auto decl = asDeclaration(item);
    if (!decl) {
      continue;
    }
    GpioPort port;
    port.name = decl->getName();
    port.index = index++;
    port.width = static_cast<int>(intValue(decl->getPropertyValue("width")).value_or(8));
    port.output = intValue(decl->getPropertyValue("output")).value_or(0);
    port.input = intValue(decl->getPropertyValue("input")).value_or(0);
    port.direction = intValue(decl->getPropertyValue("direction"));
    port.outputSet = intValue(decl->getPropertyValue("outputSet"));
    port.outputClear = intValue(decl->getPropertyValue("outputClear"));
    port.outputToggle = intValue(decl->getPropertyValue("outputToggle"));
    port.directionSet = intValue(decl->getPropertyValue("directionSet"));
    port.directionClear = intValue(decl->getPropertyValue("directionClear"));
    port.setShift = static_cast<int>(intValue(decl->getPropertyValue("setShift")).value_or(0));
    port.clearShift = static_cast<int>(intValue(decl->getPropertyValue("clearShift")).value_or(0));
    if (auto enable = decl->getPropertyValue("enable")) {
      port.enableRegister = intValue(anonymousBlockValue(enable, "register"));
      port.enableBit = static_cast<int>(
          intValue(anonymousBlockValue(enable, "bit")).value_or(0));
    }
    port.driverAccess = optionName(decl->getPropertyValue("access")) == "AccessDriver";
    for (const auto &fieldItem : listItems(decl->getPropertyValue("fields"))) {
      auto fieldDecl = asDeclaration(fieldItem);
      if (!fieldDecl) {
        continue;
      }
      GpioField field;
      field.name = fieldDecl->getName();
      field.file = fieldDecl->getFilename();
      field.line = fieldDecl->getLine();
      field.option = optionName(fieldDecl->getPropertyValue("option"));
      field.reg = intValue(fieldDecl->getPropertyValue("register")).value_or(0);
      field.regHigh = intValue(fieldDecl->getPropertyValue("registerHigh"));
      field.bitsPerPin = static_cast<int>(intValue(fieldDecl->getPropertyValue("bitsPerPin")).value_or(1));
      field.shift = static_cast<int>(intValue(fieldDecl->getPropertyValue("shift")).value_or(0));
      field.pinStride = intValue(fieldDecl->getPropertyValue("pinStride")).value_or(0);
      field.options = nameList(fieldDecl->getPropertyValue("options"));
      for (const auto &code : listItems(fieldDecl->getPropertyValue("codes"))) {
        field.codes.push_back(intValue(code).value_or(0));
      }
      port.fields.push_back(field);
    }
    ports.push_back(port);
  }
  return ports;
}

std::optional<GpioPort> ResolvedHardware::gpioPort(const std::string &name) const {
  for (const auto &port : gpioPorts()) {
    if (port.name == name) {
      return port;
    }
  }
  return std::nullopt;
}

std::vector<BoardPin> ResolvedHardware::boardPins() const {
  // pinFeatures by port and bit
  std::map<std::pair<std::string, int64_t>, std::shared_ptr<DeclarationNode>> features;
  for (const auto &item : listItems(property("pinFeatures"))) {
    if (auto decl = asDeclaration(item)) {
      auto port = HardwareResolver::valueText(decl->getPropertyValue("port"));
      auto bit = intValue(decl->getPropertyValue("bit"));
      if (!port.empty() && bit) {
        features[{port, *bit}] = decl;
      }
    }
  }
  std::vector<BoardPin> pins;
  for (const auto &item : listItems(property("pins"))) {
    auto decl = asDeclaration(item);
    if (!decl) {
      continue;
    }
    BoardPin pin;
    pin.label = decl->getName();
    pin.port = HardwareResolver::valueText(decl->getPropertyValue("port"));
    if (pin.port == "none") {
      pin.port.clear();
    }
    pin.bit = static_cast<int>(intValue(decl->getPropertyValue("bit")).value_or(-1));
    auto package = decl->getPropertyValue("packagePin");
    if (package && package->getNodeType() == AST::String) {
      pin.package = HardwareResolver::valueText(package);
    }
    pin.analog = intValue(decl->getPropertyValue("adc"));
    auto feature = features.find({pin.port, pin.bit});
    if (feature != features.end()) {
      auto f = feature->second;
      pin.modes = nameList(f->getPropertyValue("modes"));
      pin.interrupts = nameList(f->getPropertyValue("interrupts"));
      if (auto isrc = asDeclaration(f->getPropertyValue("interruptSource"))) {
        auto vecProp = isrc->getPropertyValue("vector");
        if (vecProp && vecProp->getNodeType() == AST::String) {
          pin.interruptVector = HardwareResolver::valueText(vecProp);
        }
      }
      if (auto analog = intValue(f->getPropertyValue("analog"))) {
        pin.analog = analog;
      }
      for (const auto &routeItem : listItems(f->getPropertyValue("routes"))) {
        if (auto routeDecl = asDeclaration(routeItem)) {
          pin.routes.push_back(routeFrom(routeDecl));
        }
      }
      auto reserved = f->getPropertyValue("reservedBy");
      if (reserved && reserved->getNodeType() == AST::String) {
        pin.reservedBy = HardwareResolver::valueText(reserved);
      }
    }
    for (const auto &routeItem : listItems(anonymousBlockValue(property("gpio"), "anyPinRoutes"))) {
      if (auto routeDecl = asDeclaration(routeItem)) {
        pin.routes.push_back(routeFrom(routeDecl));
      }
    }
    pins.push_back(pin);
  }
  return pins;
}

std::optional<BoardPin> ResolvedHardware::boardPin(const std::string &label) const {
  for (const auto &pin : boardPins()) {
    if (pin.label == label) {
      return pin;
    }
  }
  return std::nullopt;
}

std::vector<std::string>
ResolvedHardware::supportedValues(const BoardPin &pin,
                                  const std::string &option) const {
  std::vector<std::string> values;
  auto add = [&](const std::string &value) {
    if (!value.empty() && !contains(values, value)) {
      values.push_back(value);
    }
  };
  static const std::map<std::string, std::string> defaults = {
      {"_PinOutputTypes", "OutputPushPull"}, {"_PinPulls", "PullNone"},
      {"_PinDrives", "DriveDefault"},        {"_PinSlews", "SlewDefault"},
      {"_PinInterrupts", "InterruptNone"},   {"_PinIoStandards", "IoDefault"}};
  auto def = defaults.find(option);
  if (def != defaults.end()) {
    add(def->second);
  }
  auto port = gpioPort(pin.port);
  if (port) {
    if (option == "_PinModes" &&
        (port->direction || (port->directionSet && port->directionClear))) {
      add("PinInput");
      add("PinOutput");
    }
    for (const auto &field : port->fields) {
      if (field.option == option) {
        for (size_t i = 0; i < field.options.size() && i < field.codes.size(); ++i) {
          add(field.options[i]);
        }
      }
    }
  }
  if (option == "_PinModes") {
    for (const auto &mode : pin.modes) {
      add(mode);
    }
    if (pin.analog) {
      add("PinAnalog");
    }
    if (!pin.routes.empty()) {
      add("PinFunction");
    }
  }
  if (option == "_PinInterrupts") {
    for (const auto &kind : nameList(anonymousBlockValue(property("gpio"), "interrupts"))) {
      add(kind);
    }
    for (const auto &kind : pin.interrupts) {
      add(kind);
    }
  }
  for (const auto &value : nameList(anonymousBlockValue(property("gpio"), "emulated"))) {
    // emulated lists values of any option; keep those of this one
    static const std::map<std::string, std::string> owner = {
        {"OutputOpenDrain", "_PinOutputTypes"},
        {"OutputOpenSource", "_PinOutputTypes"},
        {"PullUp", "_PinPulls"},
        {"PullDown", "_PinPulls"}};
    auto it = owner.find(value);
    if (it != owner.end() && it->second == option) {
      add(value);
    }
  }
  return values;
}

void HardwareResolver::addSearchDirectory(const std::string &dir) {
  if (!dir.empty() && !contains(m_searchDirectories, dir)) {
    m_searchDirectories.push_back(dir);
  }
}

std::vector<GpioError>
HardwareResolver::checkGpioPins(const ResolvedHardware &hardware,
                                const std::vector<GpioPinConfig> &pins) {
  std::vector<GpioError> errors;
  const std::string device = hardware.layerName(HardwareKind::Device);
  struct Use {
    const GpioPinConfig *pin;
    std::string chipPin;
  };
  std::vector<Use> uses;

  // The device's own description: options and codes are parallel lists.
  for (const auto &port : hardware.gpioPorts()) {
    for (const auto &field : port.fields) {
      if (field.options.size() != field.codes.size()) {
        errors.push_back({field.file, field.line,
                          "gpioField " + field.name + " of " + port.name + " has " +
                              std::to_string(field.options.size()) + " options but " +
                              std::to_string(field.codes.size()) + " codes"});
      }
    }
  }

  for (const auto &pin : pins) {
    auto fail = [&](const std::string &message) {
      errors.push_back({pin.file, pin.line, "gpioPin " + pin.name + ": " + message});
    };
    if (!pin.boardQualifier.empty() && !hardware.hasLayer(pin.boardQualifier)) {
      fail("'" + pin.boardQualifier + "::" + pin.label + "' names " +
           pin.boardQualifier + ", which is not in the overlay chain of " +
           hardware.name());
      continue;
    }
    auto boardPin = hardware.boardPin(pin.label);
    if (!boardPin) {
      fail("board " + hardware.name() + " has no pin '" + pin.label + "'");
      continue;
    }
    if (pin.mode.empty()) {
      fail("mode is required");
      continue;
    }
    if (boardPin->port.empty()) {
      fail("pin '" + pin.label + "' is the package ball " +
           (boardPin->package.empty() ? std::string("(none)") : boardPin->package) +
           "; package pins need an FPGA backend, which is not supported yet");
      continue;
    }
    auto port = hardware.gpioPort(boardPin->port);
    if (!port) {
      fail("pin '" + pin.label + "' names port " + boardPin->port + ", which " +
           device + " does not declare");
      continue;
    }
    if (port->driverAccess) {
      fail("port " + port->name + " is reached through a driver (AccessDriver), "
           "which the GPIO modules don't support yet");
      continue;
    }
    std::string chipPin = device + " pin " + port->name + "." + std::to_string(boardPin->bit);
    if (!boardPin->reservedBy.empty()) {
      fail("pin '" + pin.label + "' (" + chipPin + ") is reserved by " +
           boardPin->reservedBy + ", which the runtime uses");
      continue;
    }

    for (const auto &[key, typeClass] : optionKeys()) {
      auto value = valueOf(pin, key);
      auto supported = hardware.supportedValues(*boardPin, typeClass);
      if (!contains(supported, value)) {
        fail(key + " " + value + " is not supported by " + chipPin +
             " (supported: " + joined(supported) + ")");
      }
    }
    if (pin.hysteresis) {
      bool hasField = std::any_of(port->fields.begin(), port->fields.end(),
                                  [](const GpioField &f) { return f.option == "hysteresis"; });
      if (!hasField) {
        fail("hysteresis can't be changed on " + chipPin);
      }
    }

    // Keys that must fit the mode
    if (!isOutput(pin)) {
      if (pin.outputType != "OutputPushPull") fail("outputType is set, but mode is " + pin.mode);
      if (pin.drive != "DriveDefault") fail("drive is set, but mode is " + pin.mode);
      if (pin.slew != "SlewDefault") fail("slew is set, but mode is " + pin.mode);
      if (pin.initial) fail("initial is set, but mode is " + pin.mode);
    }
    if ((pin.mode == "PinAnalog" || pin.mode == "PinDisabled") && pin.pull != "PullNone") {
      fail("pull is set, but mode is " + pin.mode);
    }
    if (pin.mode == "PinFunction") {
      if (pin.function.empty()) {
        fail("mode PinFunction needs a function");
      } else {
        bool routed = std::any_of(boardPin->routes.begin(), boardPin->routes.end(),
                                  [&](const PinRoute &r) { return r.function == pin.function; });
        if (!routed) {
          std::vector<std::string> offered;
          for (const auto &r : boardPin->routes) offered.push_back(r.function);
          fail("function " + pin.function + " can't be routed to " + chipPin +
               " (it offers: " + joined(offered) + ")");
        }
      }
    } else if (!pin.function.empty()) {
      fail("function is set, but mode is " + pin.mode);
    }
    uses.push_back({&pin, chipPin});
  }

  // Two gpioPins on one chip pin must agree
  for (size_t i = 0; i < uses.size(); ++i) {
    for (size_t j = i + 1; j < uses.size(); ++j) {
      if (uses[i].chipPin != uses[j].chipPin) {
        continue;
      }
      const auto &a = *uses[i].pin;
      const auto &b = *uses[j].pin;
      bool same = a.mode == b.mode && a.outputType == b.outputType &&
                  a.pull == b.pull && a.drive == b.drive && a.slew == b.slew &&
                  a.function == b.function && a.initial == b.initial &&
                  a.activeLow == b.activeLow;
      if (!same) {
        errors.push_back({b.file, b.line,
                          "gpioPin " + b.name + " uses " + uses[j].chipPin +
                              ", which gpioPin " + a.name +
                              " also uses with different settings"});
      }
    }
  }
  return errors;
}

std::optional<PinAccess>
HardwareResolver::resolvePinAccess(const ResolvedHardware &hardware,
                                   const GpioPinConfig &pin) {
  auto boardPin = hardware.boardPin(pin.label);
  if (!boardPin || boardPin->bit < 0) {
    return std::nullopt;
  }
  auto port = hardware.gpioPort(boardPin->port);
  if (!port) {
    return std::nullopt;
  }
  PinAccess access;
  access.width = port->width;
  access.outAddress = port->output;
  access.outMask = int64_t(1) << boardPin->bit;
  access.inAddress = port->input;
  access.inMask = access.outMask;
  if (port->outputSet && port->outputClear) {
    access.writeStyle = "WriteSetClear";
    access.setAddress = *port->outputSet;
    access.clearAddress = *port->outputClear;
    access.setMask = int64_t(1) << (boardPin->bit + port->setShift);
    access.clearMask = int64_t(1) << (boardPin->bit + port->clearShift);
  } else {
    access.writeStyle = "WriteReadModify";
    access.setAddress = port->output;
    access.clearAddress = port->output;
    access.setMask = access.outMask;
    access.clearMask = access.outMask;
  }
  access.invert = pin.activeLow;
  if (pin.outputType == "OutputOpenDrain") {
    bool hasField = false;
    for (const auto &field : port->fields) {
      if (field.option == "_PinOutputTypes" && field.codeFor("OutputOpenDrain")) {
        hasField = true;
      }
    }
    if (!hasField && port->direction) {
      access.openDrainEmulated = true;
      access.dirAddress = *port->direction;
      access.dirMask = access.outMask;
    }
  }
  return access;
}

std::vector<RegisterWrite>
HardwareResolver::pinSetupWrites(const ResolvedHardware &hardware,
                                 const std::vector<GpioPinConfig> &pins) {
  // Steps, in order; writes in one step to one register are merged.
  enum Step { Enable, Level, Pad, Function, Mode, StepCount };
  std::vector<std::vector<RegisterWrite>> steps(StepCount);
  auto modify = [&](Step step, int64_t address, int width, uint64_t clear,
                    uint64_t set, const std::string &reason) {
    for (auto &w : steps[step]) {
      if (w.address == address && w.readModify) {
        w.clearMask |= clear;
        w.setBits = (w.setBits & ~clear) | set;
        w.reason += ", " + reason;
        return;
      }
    }
    steps[step].push_back({address, width, true, clear, set, reason});
  };
  auto store = [&](Step step, int64_t address, int width, uint64_t value,
                   const std::string &reason) {
    steps[step].push_back({address, width, false, 0, value, reason});
  };
  auto writeField = [&](Step step, const GpioField &field, int bit, int width,
                        int64_t code, const std::string &reason) {
    auto [address, offset] = fieldLocation(field, bit, width);
    uint64_t mask = fieldMask(field) << offset;
    modify(step, address, width, mask, (uint64_t(code) << offset) & mask, reason);
  };

  std::set<std::string> enabledPorts;
  for (const auto &pin : pins) {
    auto boardPin = hardware.boardPin(pin.label);
    if (!boardPin || boardPin->bit < 0) {
      continue;
    }
    auto port = hardware.gpioPort(boardPin->port);
    auto access = resolvePinAccess(hardware, pin);
    if (!port || !access) {
      continue;
    }
    const int bit = boardPin->bit;
    const int width = port->width;
    const uint64_t pinMask = uint64_t(1) << bit;

    if (port->enableRegister && enabledPorts.insert(port->name).second) {
      modify(Enable, *port->enableRegister, width, uint64_t(1) << port->enableBit,
             uint64_t(1) << port->enableBit, port->name + " clock");
    }

    // Initial level, before the pin becomes an output
    if (isOutput(pin)) {
      bool high = pin.initial != pin.activeLow;
      if (access->openDrainEmulated) {
        modify(Level, port->output, width, pinMask, 0, pin.name + " output low");
      } else if (access->writeStyle == "WriteSetClear") {
        store(Level, high ? access->setAddress : access->clearAddress, width,
              uint64_t(high ? access->setMask : access->clearMask),
              pin.name + (high ? " high" : " low"));
      } else {
        modify(Level, port->output, width, pinMask, high ? pinMask : 0,
               pin.name + (high ? " high" : " low"));
      }
    }

    // Pad fields: written only when not the reset default
    for (const auto &field : port->fields) {
      std::string value;
      if (field.option == "_PinPulls") value = pin.pull;
      else if (field.option == "_PinDrives") value = pin.drive;
      else if (field.option == "_PinSlews") value = pin.slew;
      else if (field.option == "_PinOutputTypes") value = pin.outputType;
      else if (field.option == "_PinIoStandards") value = pin.ioStandard;
      else continue;
      if (isDefaultValue(value) && !(field.option == "_PinOutputTypes" && isOutput(pin))) {
        continue;
      }
      if (auto code = field.codeFor(value)) {
        writeField(Pad, field, bit, width, *code, pin.name + " " + value);
      }
    }

    // Function select
    if (pin.mode == "PinFunction") {
      for (const auto &route : boardPin->routes) {
        if (route.function != pin.function) {
          continue;
        }
        for (const auto &field : port->fields) {
          if (field.option == "_PinFunctionType" && route.select) {
            writeField(Function, field, bit, width, *route.select,
                       pin.name + " " + pin.function);
          }
        }
        if (route.reg) {
          store(Function, *route.reg, 32, uint64_t(port->index * port->width + bit),
                pin.name + " " + pin.function + " route");
        }
        break;
      }
    } else {
      for (const auto &field : port->fields) {
        if (field.option == "_PinFunctions") {
          if (auto code = field.codeFor("PinGpio")) {
            writeField(Function, field, bit, width, *code, pin.name + " GPIO");
          }
        }
      }
    }

    // Mode and direction, last. Emulated open drain drives the direction
    // bit itself: released (high) is an input, low is an output.
    bool modeField = false;
    if (access->openDrainEmulated) {
      bool drivesLow = !(pin.initial != pin.activeLow);
      modify(Mode, access->dirAddress, width, pinMask, drivesLow ? pinMask : 0,
             pin.name + (drivesLow ? " drives low" : " released"));
      continue;
    }
    for (const auto &field : port->fields) {
      if (field.option == "_PinModes") {
        if (auto code = field.codeFor(pin.mode)) {
          writeField(Mode, field, bit, width, *code, pin.name + " " + pin.mode);
          modeField = true;
        }
      }
    }
    if (!modeField && pin.mode != "PinFunction") {
      bool output = isOutput(pin);
      if (port->direction) {
        modify(Mode, *port->direction, width, pinMask, output ? pinMask : 0,
               pin.name + (output ? " output" : " input"));
      } else if (port->directionSet && port->directionClear) {
        store(Mode, output ? *port->directionSet : *port->directionClear, width,
              pinMask, pin.name + (output ? " output" : " input"));
      }
    }
  }

  std::vector<RegisterWrite> writes;
  for (const auto &step : steps) {
    writes.insert(writes.end(), step.begin(), step.end());
  }
  return writes;
}

} // namespace strd
