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

#ifndef BASEPROJECT_H
#define BASEPROJECT_H

#include "stride/parser/ast.h"
#include "systemconfiguration.hpp"

#include <functional>

#define STRIDE_PLUGIN_MAX_STR_LEN 32

namespace strd {
class Builder;

typedef Builder *(*create_object_t)(std::string projectDir,
                                    std::string strideRoot,
                                    std::string platformPath);
typedef void (*platform_name_t)(char *name);
typedef int (*platform_version_major_t)();
typedef int (*platform_version_minor_t)();

typedef struct {
  create_object_t create;
  platform_name_t get_name;
  platform_version_major_t get_version_major;
  platform_version_minor_t get_version_minor;
} PluginInterface;

class StrideSystem;

class Builder {
public:
  Builder(std::string projectDir, std::string strideRoot,
          std::string platformPath)
      : m_projectDir(projectDir), m_strideRoot(strideRoot),
        m_platformPath(platformPath) {}
  virtual ~Builder() {}

  std::string getPlatformPath() { return m_platformPath; }
  virtual std::string getStdErr() const = 0;
  virtual std::string getStdOut() const = 0;
  virtual void clearBuffers() = 0;

  // Additional information from stride system
  std::shared_ptr<StrideSystem>
      m_system; // The StrideSystem that created this builder.
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
  //    // TODO this would need to send encrypted strings or remove the code
  //    sections? virtual QString requestTypesJson() {return "";} virtual
  //    QString requestFunctionsJson() {return "";} virtual QString
  //    requestObjectsJson() {return "";}
  virtual bool isValid() { return false; }

protected:
  std::string m_projectDir;
  std::string m_strideRoot;
  std::string m_platformPath;

  std::string substituteTokens(std::string text);

private:
  PluginInterface m_interface;
  //  QLibrary *m_pluginLibrary;
  //  Builder *m_project;
};

inline std::string Builder::substituteTokens(std::string text) {
  std::map<std::string, std::string> tokenMap;
  tokenMap["%projectDir%"] = m_projectDir;
  tokenMap["%strideRoot%"] = m_strideRoot;
  tokenMap["%platformRoot%"] = m_platformPath;
  tokenMap["%projectName%"] = m_platformPath;
  for (const auto &mapEntry : tokenMap) {
    size_t startPos;
    while ((startPos = text.find(mapEntry.first.c_str())) !=
           std::string::npos) {
      text.replace(startPos, mapEntry.first.size(), tokenMap[mapEntry.second]);
    }
  }
  return text;
}
} // namespace strd

#endif // BASEPROJECT_H
