#include "token_bytes.h"
#include <gtest/gtest.h>

using sessio::litert::detail::sentencePieceTokenBytes;

TEST(LiteRtTokenBytes, ByteFallbackPiecesPreserveAnIncompleteUtf8Prefix) {
  EXPECT_EQ(sentencePieceTokenBytes("<0xD0>"), std::string(1, '\xd0'));
  EXPECT_EQ(sentencePieceTokenBytes("<0xD0>") + sentencePieceTokenBytes("<0x91>") + "орис", "Борис");
  EXPECT_EQ(sentencePieceTokenBytes("<0x00>"), std::string(1, '\0'));
  EXPECT_EQ(sentencePieceTokenBytes("<0xFF>"), std::string(1, '\xff'));
}

TEST(LiteRtTokenBytes, SentencePieceMetaspaceIsDecodedToAnOrdinarySpace) {
  EXPECT_EQ(sentencePieceTokenBytes("▁встреча"), " встреча");
  EXPECT_EQ(sentencePieceTokenBytes("▁в▁полдень"), " в полдень");
}

TEST(LiteRtTokenBytes, TextControlTokensAndMalformedBytePiecesStayIntact) {
  for (const auto *piece : {"Борис", "<turn|>", "<eos>", "<0xGG>", "<0x0>", "prefix<0xD0>"})
    EXPECT_EQ(sentencePieceTokenBytes(piece), piece);
}
