#include "config.h"

#include <Poco/File.h>

namespace pcm::config {

void Config::save_config(const Config &conf) {
    Poco::File(Poco::Path(conf.config_pth.value()).makeParent()).createDirectories();
    rfl::yaml::save(conf.config_pth.value().toString(), conf);
}

Config Config::read_config() {
    const auto default_pth = Config().config_pth.value();
    if (!Poco::File(default_pth).exists()) {
        return Config();
    }
    return rfl::yaml::load<Config>(default_pth.toString()).value();
}

void Config::migrate_legacy_directory() {
    const auto legacyDir =
        Poco::Path(Poco::Path::configHome()).append(kLegacyAppDirName);
    const auto newDir = Poco::Path(Poco::Path::configHome()).append(kAppDirName);

    Poco::File legacyFile(legacyDir);
    const Poco::File newFile(newDir);
    if (legacyFile.exists() && !newFile.exists()) {
        legacyFile.renameTo(newDir.toString());
    }
}

} // namespace pcm::config
