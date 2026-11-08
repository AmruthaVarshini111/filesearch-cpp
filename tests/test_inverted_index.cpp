#include <gtest/gtest.h>

#include "search/inverted_index.hpp"
#include "search/term_extractor.hpp"

using namespace filesearch;

namespace {
InvertedIndex build() {
  InvertedIndex idx;
  idx.add_document(1, extract_terms("machine-learning-notes.txt", "/docs/ml").terms);
  idx.add_document(2, extract_terms("learning.pdf", "/docs/books").terms);
  idx.add_document(3, extract_terms("holiday.jpg", "/photos/machine").terms);
  return idx;
}
}  // namespace

TEST(TermExtractor, SeparatesNameAndPathFields) {
  const auto doc = extract_terms("learning-learning.pdf", "/docs/learning");
  EXPECT_EQ(doc.name_token_count, 3);
  for (const auto& t : doc.terms) {
    if (t.term == "learning") {
      EXPECT_EQ(t.tf_name, 2u);
      EXPECT_EQ(t.tf_path, 1u);
      return;
    }
  }
  FAIL() << "term 'learning' missing";
}

TEST(TermExtractor, TermsAreSortedAndUnique) {
  const auto doc = extract_terms("a-b-a.txt", "/a");
  for (size_t i = 1; i < doc.terms.size(); ++i) EXPECT_LT(doc.terms[i - 1].term, doc.terms[i].term);
}

TEST(InvertedIndex, BuildsPostingsPerTerm) {
  const auto idx = build();
  EXPECT_EQ(idx.document_count(), 3u);
  EXPECT_EQ(idx.document_frequency("learning"), 2u);
  EXPECT_EQ(idx.document_frequency("machine"), 2u);  // file name of doc 1, directory of doc 3
  EXPECT_EQ(idx.document_frequency("holiday"), 1u);
  EXPECT_EQ(idx.document_frequency("missing"), 0u);
  EXPECT_EQ(idx.postings("missing"), nullptr);
}

TEST(InvertedIndex, PostingsRecordTheField) {
  const auto idx = build();
  const auto* list = idx.postings("machine");
  ASSERT_NE(list, nullptr);
  for (const auto& p : *list) {
    if (p.doc_id == 1) {
      EXPECT_EQ(p.tf_name, 1u);
      EXPECT_EQ(p.tf_path, 0u);
    } else {
      EXPECT_EQ(p.doc_id, 3);
      EXPECT_EQ(p.tf_name, 0u);
      EXPECT_EQ(p.tf_path, 1u);
    }
  }
}

TEST(InvertedIndex, RemoveDocumentDropsItsPostings) {
  auto idx = build();
  idx.remove_document(2);
  EXPECT_EQ(idx.document_count(), 2u);
  EXPECT_EQ(idx.document_frequency("learning"), 1u);
  EXPECT_EQ(idx.postings("books"), nullptr);  // term vanished with its last posting
  idx.remove_document(999);                   // unknown id is a no-op
}

TEST(InvertedIndex, ReAddingADocumentReplacesIt) {
  auto idx = build();
  idx.add_document(1, extract_terms("renamed.txt", "/docs").terms);
  EXPECT_EQ(idx.document_frequency("renamed"), 1u);
  EXPECT_EQ(idx.document_frequency("notes"), 0u);
  EXPECT_EQ(idx.document_count(), 3u);
}

TEST(InvertedIndex, VocabularyMatchingFindsSubstrings) {
  const auto idx = build();
  const auto m = idx.vocabulary_matching("earn");
  ASSERT_EQ(m.size(), 1u);
  EXPECT_EQ(m[0].first, "learning");
  EXPECT_EQ(m[0].second, 2u);
}
