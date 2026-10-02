#include "config.h"

#include <Poco/File.h>

namespace pcm::config {

void Config::save_config(const Config &conf) {
    Poco::File(Poco::Path(conf.config_pth.value()).makeParent()).createDirectories();
    // rfl::yaml::save reports failure (e.g. the file cannot be opened) through
    // its Result instead of throwing; .value() turns that into an exception so
    // callers' try/catch actually sees a failed save.
    rfl::yaml::save(conf.config_pth.value().toString(), conf).value();
}

Config Config::read_config() {
    const auto default_pth = Config().config_pth.value();
    if (!Poco::File(default_pth).exists()) {
        return Config();
    }
    auto conf = rfl::yaml::load<Config>(default_pth.toString()).value();
    // config_pth is an rfl::Skip field: it is not stored in the file and comes
    // back default-constructed (an empty path), not with the member
    // initializer's value. Restore it so read -> modify -> save_config writes
    // back to the same file instead of silently failing on an empty path.
    conf.config_pth = rfl::Skip<Poco::Path>(default_pth);
    return conf;
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
