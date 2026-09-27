#pragma once

#include <Poco/Path.h>
#include <rfl/Flatten.hpp>
#include <rfl/yaml.hpp>
#include "app_role.h"
#include "rfl_path.hpp"

namespace pcm::config {

constexpr auto kLegacyAppDirName = "PsyClientManager";
constexpr auto kAppDirName = "Sessio";

struct DatabaseConfig {
    Poco::Path db_pth = Poco::Path(Poco::Path::configHome())
                            .append(kAppDirName)
                            .append("database");
};

struct Config {
    rfl::Skip<Poco::Path> config_pth = Poco::Path(Poco::Path::configHome())
                                           .append(kAppDirName)
                                           .append("Config.yaml");
    rfl::Flatten<DatabaseConfig> db_conf;
    std::string app_role = "Unset";

    static void save_config(const Config &conf);
    static Config read_config();

    // One-time migration for installs that still have their database/config
    // under the pre-rename directory name. Must run before any Config is
    // constructed, since the paths above are baked in as default values.
    static void migrate_legacy_directory();
};

} // namespace pcm::config