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

#include "stride/stridegen/plugins/pluginloader.hpp"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/utils/logger.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#else
#include <dlfcn.h>
#endif

using namespace strd;

namespace {

#ifdef _WIN32
struct DllDirectoryGuard {
  DllDirectoryGuard(const std::wstring &dir) {
    DWORD needed = GetDllDirectoryW(0, nullptr);
    if (needed > 0) {
      previous.resize(needed, L'\0');
      DWORD len = GetDllDirectoryW(needed, previous.data());
      previous.resize(len);
      hadPrevious = true;
    }
    SetDllDirectoryW(dir.c_str());
  }
  ~DllDirectoryGuard() {
    SetDllDirectoryW(hadPrevious ? previous.c_str() : nullptr);
  }
  std::wstring previous;
  bool hadPrevious{false};
};
#endif

std::shared_ptr<void> openSharedLibrary(const std::filesystem::path &path,
                                        std::string &outError) {
#ifdef _WIN32
  DllDirectoryGuard dllGuard(path.parent_path().wstring());
  HMODULE handle = LoadLibraryW(path.filename().wstring().c_str());
  if (!handle) {
    outError = "Win32 error " + std::to_string(GetLastError());
    return nullptr;
  }
  return std::shared_ptr<void>(handle, [](void *p) {
    if (p) {
      FreeLibrary(static_cast<HMODULE>(p));
    }
  });
#else
  void *handle = dlopen(path.c_str(), RTLD_NOW);
  if (!handle) {
    const char *err = dlerror();
    outError = err ? err : "unknown dlopen error";
    return nullptr;
  }
  return std::shared_ptr<void>(handle, [](void *p) {
    if (p) {
      dlclose(p);
    }
  });
#endif
}

void *lookupSymbol(const std::shared_ptr<void> &lib, const char *symbol) {
  if (!lib) {
    return nullptr;
  }
#ifdef _WIN32
  return reinterpret_cast<void *>(
      GetProcAddress(static_cast<HMODULE>(lib.get()), symbol));
#else
  return dlsym(lib.get(), symbol);
#endif
}

} // namespace

std::string
PluginLoader::ensureProductsDirectory(const std::string &fileName) {
  if (fileName.empty()) {
    return {};
  }
  std::error_code ec;
  auto canonicalFile = std::filesystem::weakly_canonical(fileName, ec);
  if (ec) {
    return {};
  }
  std::filesystem::path dirPath(canonicalFile.string() + "_Products");
  if (!std::filesystem::exists(dirPath, ec)) {
    if (!std::filesystem::create_directories(dirPath, ec) || ec) {
      return {};
    }
  }
  return dirPath.string();
}

std::vector<LoadedPluginBuilder>
PluginLoader::loadBuilders(StrideSystem &system, const std::string &fileName,
                           const ASTNode &tree) {
  std::vector<LoadedPluginBuilder> builders;
  std::string projectDir = ensureProductsDirectory(fileName);
  if (projectDir.empty()) {
    LOG_ERROR() << "Error creating project products directory for '" << fileName
                << "'" << std::endl;
    return builders;
  }

  const auto usedFrameworks = CodeAnalysis::getUsedFrameworks(tree);
  const std::filesystem::path pluginPath =
      std::filesystem::path(system.getStrideRoot()) / "plugins";

  for (const auto &platform : system.getFrameworks()) {
    if (!platform) {
      continue;
    }
    const std::string fwAlias =
        system.getFrameworkAlias(platform->getFramework());
    if (!usedFrameworks.empty() &&
        std::find(usedFrameworks.begin(), usedFrameworks.end(), fwAlias) ==
            usedFrameworks.end()) {
      continue;
    }

    if (platform->getAPI() != StrideFramework::PluginPlatform) {
      LOG_WARNING() << "Unsupported builder API for framework '"
                    << platform->getFramework() << "'" << std::endl;
      continue;
    }

    std::string pluginName = "pufferfish";
    int pluginMajorVersion = 1;
    int pluginMinorVersion = 0;
    if (!platform->getPluginDetails(pluginName, pluginMajorVersion,
                                    pluginMinorVersion)) {
      LOG_DEBUG() << "No explicit plugin details for framework '"
                  << platform->getFramework() << "', using default" << std::endl;
    }

    std::error_code ec;
    if (!std::filesystem::exists(pluginPath, ec) ||
        !std::filesystem::is_directory(pluginPath, ec)) {
      continue;
    }

    for (const auto &entry :
         std::filesystem::directory_iterator(pluginPath, ec)) {
      if (ec || !entry.is_regular_file(ec)) {
        continue;
      }
#ifdef _WIN32
      if (entry.path().extension() != ".dll") {
        continue;
      }
#endif
      std::string loadError;
      auto lib = openSharedLibrary(entry.path(), loadError);
      if (!lib) {
        LOG_ERROR() << "Error loading plugin library '"
                    << entry.path().string() << "': " << loadError << std::endl;
        continue;
      }

      char name[STRIDE_PLUGIN_MAX_STR_LEN] = {};
      auto nameFunc =
          reinterpret_cast<platform_name_t>(lookupSymbol(lib, "platform_name"));
      if (!nameFunc) {
        continue;
      }
      nameFunc(name);
      name[STRIDE_PLUGIN_MAX_STR_LEN - 1] = '\0';
      if (std::strncmp(name, pluginName.c_str(), STRIDE_PLUGIN_MAX_STR_LEN) !=
          0) {
        continue;
      }

      int versionMajor = -1;
      int versionMinor = -1;
      if (auto fn = reinterpret_cast<platform_version_major_t>(
              lookupSymbol(lib, "platform_version_major"))) {
        versionMajor = fn();
      }
      if (auto fn = reinterpret_cast<platform_version_minor_t>(
              lookupSymbol(lib, "platform_version_minor"))) {
        versionMinor = fn();
      }
      if (versionMajor != pluginMajorVersion ||
          versionMinor != pluginMinorVersion) {
        continue;
      }

      auto create =
          reinterpret_cast<create_object_t>(lookupSymbol(lib, "create_object"));
      if (!create) {
        continue;
      }
      std::unique_ptr<Builder> builder(create(
          platform->buildPlatformPath(), system.getStrideRoot(), projectDir));
      if (builder) {
        builder->m_frameworkName = platform->getFramework();
        builders.push_back(LoadedPluginBuilder{std::move(lib),
                                               std::move(builder)});
      }
    }
  }
  return builders;
}
