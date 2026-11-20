#include "markdown_chunker.h"

#include <gtest/gtest.h>

#include <string>

namespace {

using sessio::rag::Chunk;
using sessio::rag::chunkMarkdown;
using sessio::rag::kMaxChunkChars;

size_t charCount(const std::string &s) {
  size_t n = 0;
  for (unsigned char c : s)
    if ((c & 0xC0) != 0x80) ++n;
  return n;
}

std::string repeat(char c, size_t n) { return std::string(n, c); }

std::string repeatText(std::string_view unit, size_t n) {
  std::string out;
  for (size_t i = 0; i < n; ++i) out += unit;
  return out;
}

TEST(RagChunker, HeadingPathRootsAtTitleAndFollowsNesting) {
  const auto chunks = chunkMarkdown("Doc", "# A\npara1\n## B\npara2\n### C\npara3\n## D\npara4");
  ASSERT_EQ(chunks.size(), 4u);
  EXPECT_EQ(chunks[0].path, (std::vector<std::string>{"Doc", "A"}));
  EXPECT_EQ(chunks[1].path, (std::vector<std::string>{"Doc", "A", "B"}));
  EXPECT_EQ(chunks[2].path, (std::vector<std::string>{"Doc", "A", "B", "C"}));
  EXPECT_EQ(chunks[3].path, (std::vector<std::string>{"Doc", "A", "D"}));
  EXPECT_EQ(chunks[3].text, "para4");
}

TEST(RagChunker, SmallTableStaysWholeInOneChunk) {
  const auto chunks = chunkMarkdown("Doc", "# T\n| a | b |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |\nafter");
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].text, "| a | b |\n| --- | --- |\n| 1 | 2 |\n| 3 | 4 |\n\nafter");
  EXPECT_EQ(chunks[0].kinds, (std::vector<std::string>{"table", "para"}));
  EXPECT_EQ(chunks[0].path, (std::vector<std::string>{"Doc", "T"}));
}

TEST(RagChunker, OversizedTableSplitsWithHeaderRepeated) {
  std::string table = "| Name | Value |\n| --- | --- |";
  for (int i = 0; i < 40; ++i)
    table += "\n| row" + std::to_string(i) + " | " + repeat('x', 80) + " |";
  const auto chunks = chunkMarkdown("Doc", table);
  ASSERT_GE(chunks.size(), 2u);
  for (const auto &chunk : chunks) {
    EXPECT_EQ(chunk.kinds, (std::vector<std::string>{"table:split"}));
    EXPECT_TRUE(chunk.text.starts_with("| Name | Value |\n| --- | --- |"));
    EXPECT_LE(charCount(chunk.text), kMaxChunkChars);
  }
  for (int i = 0; i < 40; ++i) {
    const std::string row = "| row" + std::to_string(i) + " | " + repeat('x', 80) + " |";
    int homes = 0;
    for (const auto &chunk : chunks)
      if (chunk.text.find(row) != std::string::npos) ++homes;
    EXPECT_EQ(homes, 1) << "row " << i;
  }
}

TEST(RagChunker, CodeFenceKeepsTableAndListLookalikesInside) {
  const auto chunks = chunkMarkdown("Doc", "```\nline1\n| not a table |\n- not a list\n```\nafter");
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].kinds, (std::vector<std::string>{"code", "para"}));
  EXPECT_EQ(chunks[0].text, "```\nline1\n| not a table |\n- not a list\n```\n\nafter");
}

TEST(RagChunker, ListKeepsIndentedContinuations) {
  const auto chunks = chunkMarkdown("Doc", "- one\n  cont\n- two\n\npara");
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].kinds, (std::vector<std::string>{"list", "para"}));
  EXPECT_EQ(chunks[0].text, "- one\n  cont\n- two\n\npara");
}

TEST(RagChunker, QuoteLinesStayInOneBlock) {
  const auto chunks = chunkMarkdown("Doc", "> quoted\n> more\n\npara");
  ASSERT_EQ(chunks.size(), 1u);
  EXPECT_EQ(chunks[0].kinds, (std::vector<std::string>{"quote", "para"}));
  EXPECT_EQ(chunks[0].text, "> quoted\n> more\n\npara");
}

TEST(RagChunker, OversizedParagraphSplitsOnCharacterWindows) {
  const auto chunks = chunkMarkdown("Doc", repeat('x', 2500));
  ASSERT_EQ(chunks.size(), 2u);
  EXPECT_EQ(chunks[0].text.size(), kMaxChunkChars);
  EXPECT_EQ(chunks[1].text.size(), 2500 - kMaxChunkChars);
  EXPECT_EQ(chunks[0].kinds, (std::vector<std::string>{"para:split"}));
}

TEST(RagChunker, LengthsCountCodePointsNotBytes) {
  // 1000 Cyrillic chars = 2000 bytes: byte counting would split each block.
  const std::string block = repeatText("я", 1000);
  const auto chunks = chunkMarkdown("Doc", block + "\n\n" + block);
  ASSERT_EQ(chunks.size(), 2u);
  EXPECT_EQ(chunks[0].text, block);
  EXPECT_EQ(chunks[1].text, block);
  const auto single = chunkMarkdown("Doc", repeatText("я", kMaxChunkChars));
  ASSERT_EQ(single.size(), 1u);
  EXPECT_EQ(charCount(single[0].text), kMaxChunkChars);
}

TEST(RagChunker, EmptyAndWhitespaceTextYieldNoChunks) {
  EXPECT_TRUE(chunkMarkdown("Doc", "").empty());
  EXPECT_TRUE(chunkMarkdown("Doc", "  \n\n\t\n").empty());
}

TEST(RagChunker, GoldenFixtureMatchesSpikeSnapshot) {
  // Expectations traced from outline-chunker-spike.py chunk_structural()
  // on the same fixture (12.11 methodology, handoff 17.11 §7.1).
  const std::string fixture =
      "# Overview\n"
      "Intro paragraph about the product.\n"
      "\n"
      "## Cases\n"
      "| Client | Status |\n"
      "| --- | --- |\n"
      "| Alpha | done |\n"
      "| Beta | pilot |\n"
      "\n"
      "- item one\n"
      "  continuation line\n"
      "- item two\n"
      "\n"
      "```text\n"
      "code line one\n"
      "code line two\n"
      "```\n"
      "\n"
      "> quoted note\n"
      "\n"
      "### Deep dive\n"
      "Tail paragraph.\n";
  const auto chunks = chunkMarkdown("Fixture Doc", fixture);
  ASSERT_EQ(chunks.size(), 3u);

  EXPECT_EQ(chunks[0].text, "Intro paragraph about the product.");
  EXPECT_EQ(chunks[0].path, (std::vector<std::string>{"Fixture Doc", "Overview"}));
  EXPECT_EQ(chunks[0].kinds, (std::vector<std::string>{"para"}));

  EXPECT_EQ(chunks[1].text,
            "| Client | Status |\n| --- | --- |\n| Alpha | done |\n| Beta | pilot |"
            "\n\n- item one\n  continuation line\n- item two"
            "\n\n```text\ncode line one\ncode line two\n```"
            "\n\n> quoted note");
  EXPECT_EQ(chunks[1].path, (std::vector<std::string>{"Fixture Doc", "Overview", "Cases"}));
  EXPECT_EQ(chunks[1].kinds, (std::vector<std::string>{"table", "list", "code", "quote"}));

  EXPECT_EQ(chunks[2].text, "Tail paragraph.");
  EXPECT_EQ(chunks[2].path, (std::vector<std::string>{"Fixture Doc", "Overview", "Cases", "Deep dive"}));
  EXPECT_EQ(chunks[2].kinds, (std::vector<std::string>{"para"}));
}

} // namespace
