#pragma once

#include "token_result.h"

#include <QByteArray>
#include <optional>

namespace pcm::tokenclient {

[[nodiscard]] std::optional<TokenResult> parseTokenResponse(const QByteArray &json);
[[nodiscard]] QString parseErrorMessage(const QByteArray &json);

} // namespace pcm::tokenclient
