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

#ifndef STRIDE_PLUGINS_BUILDER_HPP
#define STRIDE_PLUGINS_BUILDER_HPP

#include "stride/parser/ast.h"
#include "stride/stridegen/systemconfiguration.hpp"

#include <functional>
#include <map>
#include <memory>
#include <string>

#define STRIDE_PLUGIN_MAX_STR_LEN 32

namespace strd {
class Builder;
class StrideSystem;

typedef Builder *(*create_object_t)(std::string projectDir,
                                    std::string strideRoot,
                                    std::string platformPath);
typedef void (*platform_name_t)(char *name);
typedef int (*platform_version_major_t)();
typedef int (*platform_version_minor_t)();

struct PluginInterface {
  create_object_t create;
  platform_name_t get_name;
  platform_version_major_t get_version_major;
  platform_version_minor_t get_version_minor;
};

class Builder {
public:
  Builder(std::string projectDir, std::string strideRoot,
          std::string platformPath)
      : m_projectDir(std::move(projectDir)),
        m_strideRoot(std::move(strideRoot)),
        m_platformPath(std::move(platformPath)) {}
  virtual ~Builder() = default;

  const std::string &getPlatformPath() const { return m_platformPath; }
  virtual std::string getStdErr() const = 0;
  virtual std::string getStdOut() const = 0;
  virtual void clearBuffers() = 0;

  std::shared_ptr<StrideSystem> m_system;
  SystemConfiguration m_systemConfiguration;
  std::string m_frameworkName;

  std::function<void(std::string)> outputText = [](std::string) {};
  std::function<void(std::string)> errorText = [](std::string) {};
  std::function<void(void)> programStopped = []() {};
  std::function<void(void)> yieldCallback = []() {};

  virtual std::map<std::string, std::string> generateCode(ASTNode tree) = 0;
  virtual bool build(std::map<std::string, std::string> domainMap) = 0;
  virtual bool deploy() = 0;
  virtual bool run(bool pressed = true) = 0;
  virtual bool isValid() { return false; }

protected:
  std::string m_projectDir;
  std::string m_strideRoot;
  std::string m_platformPath;

  std::string substituteTokens(std::string text) const;
};

inline std::string Builder::substituteTokens(std::string text) const {
  const std::pair<const char *, const std::string &> tokens[] = {
      {"%projectDir%", m_projectDir},
      {"%strideRoot%", m_strideRoot},
      {"%platformRoot%", m_platformPath},
  };
  for (const auto &[token, value] : tokens) {
    size_t startPos = 0;
    const size_t tokenLen = std::char_traits<char>::length(token);
    while ((startPos = text.find(token, startPos)) != std::string::npos) {
      text.replace(startPos, tokenLen, value);
      startPos += value.size();
    }
  }
  return text;
}
} // namespace strd

#endif // STRIDE_PLUGINS_BUILDER_HPP
