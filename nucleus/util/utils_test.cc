/*
 * Copyright 2018 Google Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "nucleus/util/utils.h"

#include "nucleus/protos/cigar.pb.h"
#include "nucleus/protos/reads.pb.h"
#include "nucleus/protos/struct.pb.h"
#include "nucleus/testing/test_utils.h"

#include "nucleus/testing/protocol-buffer-matchers.h"

#include <gmock/gmock-generated-matchers.h>
#include <gmock/gmock-matchers.h>
#include <gmock/gmock-more-matchers.h>

#include "tensorflow/core/platform/test.h"

namespace nucleus {

using nucleus::genomics::v1::CigarUnit;
using nucleus::genomics::v1::LinearAlignment;
using nucleus::genomics::v1::Read;
using nucleus::genomics::v1::ReadRequirements;
using nucleus::genomics::v1::Variant;
using nucleus::genomics::v1::VariantCall;
using ::testing::ElementsAre;
using ::testing::Eq;

// Makes an empty Variant with the given position
Variant MakeVariantAt(const string& chr, const int64 start,
                      const int64 end) {
  Variant variant;
  variant.set_reference_name(chr);
  variant.set_start(start);
  variant.set_end(end);
  return variant;
}

TEST(UtilsTest, TestAreCanonicalBases) {
  for (const auto& canon : {CanonicalBases::ACGT, CanonicalBases::ACGTN}) {
    EXPECT_TRUE(AreCanonicalBases("A", canon));
    EXPECT_TRUE(AreCanonicalBases("C", canon));
    EXPECT_TRUE(AreCanonicalBases("G", canon));
    EXPECT_TRUE(AreCanonicalBases("T", canon));
    EXPECT_FALSE(AreCanonicalBases("a", canon));
    EXPECT_FALSE(AreCanonicalBases("c", canon));
    EXPECT_FALSE(AreCanonicalBases("g", canon));
    EXPECT_FALSE(AreCanonicalBases("t", canon));
    EXPECT_TRUE(AreCanonicalBases("AA", canon));
    EXPECT_FALSE(AreCanonicalBases("Aa", canon));
    EXPECT_FALSE(AreCanonicalBases("aA", canon));
    EXPECT_TRUE(AreCanonicalBases("AC", canon));
    EXPECT_TRUE(AreCanonicalBases("AG", canon));
    EXPECT_TRUE(AreCanonicalBases("AT", canon));
    EXPECT_TRUE(AreCanonicalBases("ACGT", canon));
    EXPECT_FALSE(AreCanonicalBases("R", canon));  // R = A or G in IUPAC.
  }

  for (const string& has_n : {"N", "AN", "NA", "ANC"}) {
    EXPECT_FALSE(AreCanonicalBases(has_n));
    EXPECT_TRUE(AreCanonicalBases(has_n, CanonicalBases::ACGTN));
  }
}

TEST(UtilsTest, TestAreCanonicalBasesPosition) {
  for (size_t bad_pos = 0; bad_pos < 10; ++bad_pos) {
    string bad_bases = "ACGTACGTACGT";
    bad_bases[bad_pos] = 'R';
    size_t actual;
    EXPECT_FALSE(AreCanonicalBases(bad_bases, CanonicalBases::ACGT, &actual));
    EXPECT_THAT(actual, Eq(bad_pos));
  }
}

TEST(UtilsDeathTest, TestAreCanonicalBasesEmpty) {
  EXPECT_DEATH(AreCanonicalBases(""), "bases cannot be empty");
}

TEST(UtilsTest, TestIsCanonicalBase) {
  for (const auto& canon : {CanonicalBases::ACGT, CanonicalBases::ACGTN}) {
    EXPECT_TRUE(IsCanonicalBase('A', canon));
    EXPECT_TRUE(IsCanonicalBase('C', canon));
    EXPECT_TRUE(IsCanonicalBase('G', canon));
    EXPECT_TRUE(IsCanonicalBase('T', canon));
    EXPECT_FALSE(IsCanonicalBase('a', canon));
    EXPECT_FALSE(IsCanonicalBase('c', canon));
    EXPECT_FALSE(IsCanonicalBase('g', canon));
    EXPECT_FALSE(IsCanonicalBase('t', canon));

    // Lower-case N is always non-canonical.
    EXPECT_FALSE(IsCanonicalBase('n', canon));
  }

  // Upper-case N depends on the mode.
  EXPECT_FALSE(IsCanonicalBase('N', CanonicalBases::ACGT));
  EXPECT_TRUE(IsCanonicalBase('N', CanonicalBases::ACGTN));

  // R is always non-canonical.
  EXPECT_FALSE(IsCanonicalBase('R', CanonicalBases::ACGT));
  EXPECT_FALSE(IsCanonicalBase('R', CanonicalBases::ACGTN));
}

TEST(UtilsTest, TestMakePosition) {
  EXPECT_THAT(MakePosition("chr1", 1),
              EqualsProto("reference_name: \"chr1\" position: 1"));
  EXPECT_THAT(MakePosition("chr2", 10, true),
              EqualsProto("reference_name: \"chr2\" position: 10 "
                          "reverse_strand: true"));
}

TEST(UtilsTest, TestMakeRange) {
  EXPECT_THAT(MakeRange("chr1", 1, 10),
              EqualsProto("reference_name: \"chr1\" start: 1 end: 10"));
  EXPECT_THAT(MakeRange("chr2", 100, 1000),
              EqualsProto("reference_name: \"chr2\" start: 100 end: 1000"));
}


TEST(UtilsTest, TestRangeContains) {
  // Basic containment.
  EXPECT_TRUE(RangeContains(MakeRange("chr1", 1, 10),
                            MakeRange("chr1", 2, 5)));
  // Range contains itself...
  EXPECT_TRUE(RangeContains(MakeRange("chr1", 1, 10),
                            MakeRange("chr1", 1, 10)));
  // ... but nothing more.
  EXPECT_FALSE(RangeContains(MakeRange("chr1", 1, 10),
                             MakeRange("chr1", 1, 11)));
  EXPECT_FALSE(RangeContains(MakeRange("chr1", 1, 10),
                             MakeRange("chr1", 0, 10)));
  // Different contigs.
  EXPECT_FALSE(RangeContains(MakeRange("chr1", 1, 10),
                             MakeRange("chr2", 2, 5)));
  // Overlap is not containment.
  EXPECT_FALSE(RangeContains(MakeRange("chr1", 1, 10),
                             MakeRange("chr1", 0, 5)));
  EXPECT_FALSE(RangeContains(MakeRange("chr1", 1, 10),
                             MakeRange("chr1", 8, 15)));
  // Zero-length intervals.
  EXPECT_TRUE(RangeContains(MakeRange("chr1", 1, 10),
                            MakeRange("chr1", 1, 1)));
  EXPECT_FALSE(RangeContains(MakeRange("chr1", 1, 10),
                             MakeRange("chr1", 0, 0)));
  EXPECT_TRUE(RangeContains(MakeRange("chr1", 10, 10),
                            MakeRange("chr1", 10, 10)));
}

TEST(UtilsTest, TestMakeIntervalStr) {
  // Check that our base conversion from 0 to 1 is enabled by default
  EXPECT_EQ("chr1:2-11", MakeIntervalStr("chr1", 1, 10));
  EXPECT_EQ("chr2:3-21", MakeIntervalStr("chr2", 2, 20));

  // Test that we can emit intervals base 1
  EXPECT_EQ("chr1:1-10", MakeIntervalStr("chr1", 1, 10, false));
  EXPECT_EQ("chr2:2-20", MakeIntervalStr("chr2", 2, 20, false));

  // test some really big numbers
  EXPECT_EQ("chr3:123456789101113-123456789101114",
            MakeIntervalStr("chr3", 123456789101112, 123456789101113));

  // We handle the one-bp context specially
  EXPECT_EQ("chr2:3", MakeIntervalStr("chr2", 2, 2, true));
  EXPECT_EQ("chr2:2", MakeIntervalStr("chr2", 2, 2, false));

  // Works with Position
  EXPECT_EQ("chr2:3", MakeIntervalStr(MakePosition("chr2", 2)));

  // Works with Range
  EXPECT_EQ("chr2:3", MakeIntervalStr(MakeRange("chr2", 2, 2)));
  EXPECT_EQ("chr2:3-4", MakeIntervalStr(MakeRange("chr2", 2, 3)));
}

TEST(UtilsTest, TestComparePositions) {
  EXPECT_LT(ComparePositions(MakePosition("chr1", 1), MakePosition("chr1", 2)),
            0);
  EXPECT_EQ(ComparePositions(MakePosition("chr1", 1), MakePosition("chr1", 1)),
            0);
  EXPECT_GT(ComparePositions(MakePosition("chr1", 2), MakePosition("chr1", 1)),
            0);
  EXPECT_LT(ComparePositions(MakePosition("chr1", 2), MakePosition("chr2", 1)),
            0);
  EXPECT_GT(ComparePositions(MakePosition("chr2", 1), MakePosition("chr1", 2)),
            0);
}

TEST(UtilsTest, TestVariantPosition) {
  Variant v1 = MakeVariantAt("chr1", 1, 10);
  Variant v2 = MakeVariantAt("chr1", 1, 2);
  Variant v3 = MakeVariantAt("chr1", 1, 5);
  Variant v4 = MakeVariantAt("chr2", 10, 20);

  // Check that MakePosition only looks at reference_name and start.
  EXPECT_THAT(MakePosition(v1), EqualsProto(MakePosition("chr1", 1)));
  EXPECT_THAT(MakePosition(v2), EqualsProto(MakePosition("chr1", 1)));
  EXPECT_THAT(MakePosition(v3), EqualsProto(MakePosition("chr1", 1)));
  EXPECT_THAT(MakePosition(v4), EqualsProto(MakePosition("chr2", 10)));

  // Check that MakeRange only looks at reference_name, start, and end. Note
  // that a Range is 0-based inclusive start, exclusive stop just like the
  // Variant proto so we should exactly the same start/end values.
  EXPECT_THAT(MakeRange(v1), EqualsProto(MakeRange("chr1", 1, 10)));
  EXPECT_THAT(MakeRange(v2), EqualsProto(MakeRange("chr1", 1, 2)));
  EXPECT_THAT(MakeRange(v3), EqualsProto(MakeRange("chr1", 1, 5)));
  EXPECT_THAT(MakeRange(v4), EqualsProto(MakeRange("chr2", 10, 20)));
}

TEST(UtilsTest, TestCompareVariantPositions) {
  EXPECT_LT(ComparePositions(MakeVariantAt("chr1", 1, 2),
                             MakeVariantAt("chr1", 2, 3)),
            0);
  EXPECT_LT(ComparePositions(MakeVariantAt("chr1", 1, 5),
                             MakeVariantAt("chr1", 2, 3)),
            0);  // Check that the ends don't matter.
  EXPECT_EQ(ComparePositions(MakeVariantAt("chr1", 1, 2),
                             MakeVariantAt("chr1", 1, 2)),
            0);  // Same positions are equal.
  EXPECT_GT(ComparePositions(MakeVariantAt("chr1", 2, 3),
                             MakeVariantAt("chr1", 1, 2)),
            0);
  EXPECT_LT(ComparePositions(MakeVariantAt("chr1", 2, 3),
                             MakeVariantAt("chr2", 1, 2)),
            0);  // reference_name matters more than position.
  EXPECT_GT(ComparePositions(MakeVariantAt("chr2", 1, 2),
                             MakeVariantAt("chr1", 2, 3)),
            0);
}

TEST(UtilsTest, TestAlignedContig) {
  EXPECT_EQ("chr20", AlignedContig(MakeRead("chr20", 15, "ACTGA", {"5M"})));
  EXPECT_EQ("chr20",
            AlignedContig(MakeRead("chr20", 15, "ACTGA", {"5M", "15H"})));
  EXPECT_EQ("chrY",
            AlignedContig(MakeRead("chrY", 15, "ACTGA", {"5M", "15H"})));
  EXPECT_EQ("12", AlignedContig(MakeRead("12", 15, "ACTGA", {"5M", "15H"})));

  // Test for unaligned read.
  Read unaligned_read;
  unaligned_read.set_fragment_name("test unaligned read");
  unaligned_read.set_aligned_sequence("ATATATA");
  unaligned_read.set_number_reads(2);
  unaligned_read.set_proper_placement(true);
  EXPECT_EQ("", AlignedContig(unaligned_read));
}

TEST(UtilsTest, TestReadStart) {
  typedef std::vector<string> TestInput;
  typedef int64 TestOutput;
  const int64 start = 10000001L;
  const string& bases = "TAAACCGT";
  const std::vector<std::pair<TestInput, TestOutput>> test_data = {
      {{"8M"}, start},
      {{"1M", "3I", "4M"}, start},
      {{"5H", "1M", "3I", "3M", "19D", "1M", "10H"}, start},
      {{"5H", "1M", "3I", "19D", "1M", "3S"}, start},
      {{"2D", "8M"}, start},
  };
  for (const auto& test_case : test_data) {
    EXPECT_EQ(test_case.second,
              ReadStart(MakeRead("chr20", start, bases, test_case.first)));
  }
}

TEST(UtilsTest, TestReadEnd) {
  typedef std::vector<string> TestInput;
  typedef int64 TestOutput;
  const int64 start = 10000001L;
  const string& bases = "TAAACCGT";
  const std::vector<std::pair<TestInput, TestOutput>> test_data = {
      {{"8M"}, start + 8L},
      {{"1M", "3I", "4M"}, start + 5L},
      {{"5H", "1M", "3I", "3M", "19D", "1M", "10H"}, start + 5L + 19L},
      {{"5H", "1M", "3I", "19D", "1M", "3S"}, start + 2L + 19L},
      {{"2D", "8M"}, start + 10L},
  };
  for (const auto& test_case : test_data) {
    const Read& read = MakeRead("chr20", start, bases, test_case.first);
    EXPECT_EQ(test_case.second, ReadEnd(read));
    EXPECT_THAT(MakeRange(read),
                EqualsProto(MakeRange("chr20", start, test_case.second)));
  }
}

TEST(UtilsTest, TestIsReadProperlyPlaced) {
  Read read;
  read.set_fragment_name("read1");
  read.set_aligned_sequence("ABC");
  read.set_number_reads(2);
  read.set_proper_placement(false);  // Insert size is too small for example.
  LinearAlignment& aln = *read.mutable_alignment();
  aln.set_mapping_quality(90);
  *aln.mutable_position() = MakePosition("chr12", 10);
  EXPECT_TRUE(IsReadProperlyPlaced(read));

  // Mate mapped to different contig is improper.
  *read.mutable_next_mate_position() = MakePosition("chrY", 25);
  EXPECT_FALSE(IsReadProperlyPlaced(read));

  // Unpaired read is proper placement.
  read.set_number_reads(1);
  EXPECT_TRUE(IsReadProperlyPlaced(read));

  // Unmapped read is proper placement.
  read = Read();
  EXPECT_TRUE(IsReadProperlyPlaced(read));
}

Read ReadWithLocation(const string& chr, const int start, const int end) {
  Read read;
  LinearAlignment& aln = *read.mutable_alignment();
  *aln.mutable_position() = MakePosition(chr, start);
  CigarUnit& cigar = *aln.add_cigar();
  cigar.set_operation(CigarUnit::ALIGNMENT_MATCH);
  cigar.set_operation_length(end - start);

  return read;
}

class ReadRequirementTest : public ::testing::Test {
 protected:
  Read read_;
  ReadRequirements reqs_;

  ReadRequirementTest() {
    read_.set_fragment_name("read1");
    read_.set_aligned_sequence("ABC");
    read_.set_number_reads(2);
    read_.set_proper_placement(true);
    LinearAlignment& aln = *read_.mutable_alignment();
    aln.set_mapping_quality(90);
    *aln.mutable_position() = MakePosition("chr1", 10);
  }
};

TEST_F(ReadRequirementTest, ThatEmptyReadFailsBecauseOfNoAlignment) {
  Read read = Read();
  EXPECT_FALSE(ReadSatisfiesRequirements(read, reqs_));
  *(read.mutable_alignment()->mutable_position()) = MakePosition("chr1", 1);
  EXPECT_TRUE(ReadSatisfiesRequirements(read, reqs_));
}

TEST_F(ReadRequirementTest, TestBaseReadSatisfiesRequirements) {
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
}

TEST_F(ReadRequirementTest, TestDuplicateFilter) {
  read_.set_duplicate_fragment(true);
  EXPECT_FALSE(ReadSatisfiesRequirements(read_, reqs_));
  reqs_.set_keep_duplicates(true);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
}

TEST_F(ReadRequirementTest, TestVenderFilter) {
  read_.set_failed_vendor_quality_checks(true);
  EXPECT_FALSE(ReadSatisfiesRequirements(read_, reqs_));
  reqs_.set_keep_failed_vendor_quality_checks(true);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
}

TEST_F(ReadRequirementTest, TestSecondaryAlignmentFilter) {
  read_.set_secondary_alignment(true);
  EXPECT_FALSE(ReadSatisfiesRequirements(read_, reqs_));
  reqs_.set_keep_secondary_alignments(true);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
}

TEST_F(ReadRequirementTest, TestSupplmentaryAlignmentFilter) {
  read_.set_supplementary_alignment(true);
  EXPECT_FALSE(ReadSatisfiesRequirements(read_, reqs_));
  reqs_.set_keep_supplementary_alignments(true);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
}

TEST_F(ReadRequirementTest, TestProperPlacement) {
  // We don't use reads that aren't properly placed. Here the read's mate is
  // mapped to chrX but the read is mapped to chr1. This is an improper pair.
  read_.set_proper_placement(false);
  *read_.mutable_next_mate_position() = MakePosition("chrX", 25);
  EXPECT_FALSE(ReadSatisfiesRequirements(read_, reqs_));
  // Now the read's mate is mapped to chr1 so it is properly placed.
  *read_.mutable_next_mate_position() = MakePosition("chr1", 25);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
  reqs_.set_keep_improperly_placed(true);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
}

TEST_F(ReadRequirementTest, TestSingleEndedProperPlacement) {
  // Singled ended reads pass.
  read_.set_number_reads(1);
  read_.set_proper_placement(false);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
  reqs_.set_keep_improperly_placed(true);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
}

TEST_F(ReadRequirementTest, TestMappingQuality) {
  const int min_mapq = 10;

  // There's no minimum set, so even with mapq 0 this read should pass.
  read_.mutable_alignment()->set_mapping_quality(0);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));

  // We also keep reads with the default mapping_quality.
  read_.mutable_alignment()->clear_mapping_quality();
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));

  // Setting the min_mapping_quality now rejects the read.
  reqs_.set_min_mapping_quality(min_mapq);
  EXPECT_FALSE(ReadSatisfiesRequirements(read_, reqs_));

  // Check that the min_mapping_quality calculation is correct.
  read_.mutable_alignment()->set_mapping_quality(min_mapq - 1);
  EXPECT_FALSE(ReadSatisfiesRequirements(read_, reqs_));
  read_.mutable_alignment()->set_mapping_quality(min_mapq);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));

  // A read without a alignment but otherwise good will pass even without
  // satisfying our mapping quality as long as keep_unaligned is true.
  read_.clear_alignment();
  EXPECT_FALSE(ReadSatisfiesRequirements(read_, reqs_));
  reqs_.set_keep_unaligned(true);
  EXPECT_TRUE(ReadSatisfiesRequirements(read_, reqs_));
}

TEST(UtilsTest, TestUnquote) {
  // Common case--quotes removed
  EXPECT_EQ("foo", Unquote("\"foo\""));
  EXPECT_EQ("foo", Unquote("'foo'"));

  // Quote only on one side---Unquote returns input unchanged
  EXPECT_EQ("\"foo", Unquote("\"foo"));
  EXPECT_EQ("foo\"", Unquote("foo\""));
  EXPECT_EQ("'foo", Unquote("'foo"));
  EXPECT_EQ("foo'", Unquote("foo'"));

  // Mismatched quote delimiters---input returned unchanged
  EXPECT_EQ("\"foo'", Unquote("\"foo'"));
  EXPECT_EQ("'foo\"", Unquote("'foo\""));

  // Base cases---strings containing *just* quotes
  EXPECT_EQ("", Unquote(""));
  EXPECT_EQ("\"", Unquote("\""));
  EXPECT_EQ("", Unquote("\"\""));
  EXPECT_EQ("\"", Unquote("\"\"\""));

  EXPECT_EQ("", Unquote(""));
  EXPECT_EQ("'", Unquote("'"));
  EXPECT_EQ("", Unquote("''"));
  EXPECT_EQ("'", Unquote("'''"));
}

TEST(MapContigNameToPosInFasta, BasicCase) {
  std::vector<nucleus::genomics::v1::ContigInfo> contigs =
      CreateContigInfos({"chr1", "chr10"}, {1, 1000});
  std::map<string, int> map_name_pos = MapContigNameToPosInFasta(contigs);
  EXPECT_EQ(map_name_pos.size(), 2);
  EXPECT_EQ(map_name_pos["chr1"], 1);
  EXPECT_EQ(map_name_pos["chr10"], 1000);
}

TEST(CompareVariants, BasicCaseWithSameName) {
  std::vector<nucleus::genomics::v1::ContigInfo> contigs =
      CreateContigInfos({"xyz"}, {1});
  std::map<string, int> map_name_pos = MapContigNameToPosInFasta(contigs);
  Variant lhs;
  lhs.set_reference_name("xyz");
  lhs.set_start(1);
  lhs.set_end(2);
  Variant rhs;
  rhs.set_reference_name("xyz");
  rhs.set_start(3);
  rhs.set_end(4);
  EXPECT_TRUE(CompareVariants(lhs, rhs, map_name_pos));

  // When two things are equal, CompareVariants returns false.
  EXPECT_FALSE(CompareVariants(lhs, lhs, map_name_pos));
}

TEST(CompareVariants, BasicCaseWithSameStartDifferentEnd) {
  std::vector<nucleus::genomics::v1::ContigInfo> contigs =
      CreateContigInfos({"xyz"}, {1});
  std::map<string, int> map_name_pos = MapContigNameToPosInFasta(contigs);
  Variant lhs;
  lhs.set_reference_name("xyz");
  lhs.set_start(1);
  lhs.set_end(10);
  Variant rhs;
  rhs.set_reference_name("xyz");
  rhs.set_start(1);
  rhs.set_end(2);
  EXPECT_FALSE(CompareVariants(lhs, rhs, map_name_pos));
}

// CompareVariants compare reference_name first. If it's different, it assumes
// the one that has smaller pos_in_fasta should come first and ignore the rest.
TEST(CompareVariants, BasicCaseWithDifferentName) {
  std::vector<nucleus::genomics::v1::ContigInfo> contigs =
      CreateContigInfos({"abc", "xyz"}, {1, 1000});
  std::map<string, int> map_name_pos = MapContigNameToPosInFasta(contigs);
  Variant lhs;
  lhs.set_reference_name("abc");
  lhs.set_start(100);
  lhs.set_end(101);
  Variant rhs;
  rhs.set_reference_name("xyz");
  rhs.set_start(1);
  rhs.set_end(11);
  EXPECT_TRUE(CompareVariants(lhs, rhs, map_name_pos));
}

TEST(SetValuesValue, WorksWithInt) {
  nucleus::genomics::v1::Value value;
  int v = 10;
  SetValuesValue<int>(v, &value);
  EXPECT_THAT(value.int_value(), Eq(v));
}

TEST(SetValuesValue, WorksWithDouble) {
  nucleus::genomics::v1::Value value;
  double v = 1.23456;
  SetValuesValue<double>(v, &value);
  EXPECT_THAT(value.number_value(), Eq(v));
}

TEST(SetValuesValue, WorksWithString) {
  nucleus::genomics::v1::Value value;
  string v = "str";
  SetValuesValue<string>(v, &value);
  EXPECT_THAT(value.string_value(), Eq(v));
}

TEST(SetInfoField, WorksWithVectorOfInts) {
  VariantCall call;
  const string key = "key";
  SetInfoField<VariantCall, int>(key, std::vector<int>{1, 2}, &call);
  EXPECT_THAT(ListValues<int>(call.info().at(key)), ElementsAre(1, 2));
}

TEST(SetInfoField, WorksWithVectorOfFloats) {
  VariantCall call;
  const string key = "key";
  SetInfoField(key, std::vector<float>{1.01, 2.02}, &call);
  EXPECT_THAT(ListValues<float>(call.info().at(key)), ElementsAre(1.01, 2.02));
}

TEST(SetInfoField, WorksWithVectorOfStrings) {
  VariantCall call;
  const string key = "key";
  SetInfoField(key, std::vector<string>{"str1", "str2"}, &call);
  EXPECT_THAT(ListValues<string>(call.info().at(key)), ElementsAre("str1", "str2"));
}

TEST(SetInfoField, WorksWithSingleInt) {
  VariantCall call;
  const string key = "key";
  SetInfoField(key, 3, &call);
  EXPECT_THAT(ListValues<int>(call.info().at(key)), ElementsAre(3));
}

TEST(SetInfoField, WorksWithSingleString) {
  VariantCall call;
  const string key = "key";
  SetInfoField(key, "foo", &call);
  EXPECT_THAT(ListValues<string>(call.info().at(key)), ElementsAre("foo"));
}

TEST(SetInfoField, WorksWithVariantProto) {
  Variant variant;
  const string key = "key";
  SetInfoField(key, 3.12, &variant);
  EXPECT_THAT(ListValues<float>(variant.info().at(key)), ElementsAre(3.12));
}

TEST(SetInfoField, WorksWithSingleFloat) {
  VariantCall call;
  const string key = "key";
  SetInfoField(key, 3.12, &call);
  LOG(INFO) << "call is " << call.ShortDebugString();
  EXPECT_THAT(ListValues<float>(call.info().at(key)), ElementsAre(3.12));
}

TEST(SetInfoField, WorksWithMultipleNames) {
  VariantCall call;
  SetInfoField("key1", 3, &call);
  SetInfoField("key2", 4, &call);
  EXPECT_THAT(ListValues<int>(call.info().at("key1")), ElementsAre(3));
  EXPECT_THAT(ListValues<int>(call.info().at("key2")), ElementsAre(4));
}

TEST(SetInfoField, WorksWithOverwrite) {
  VariantCall call;
  SetInfoField("key", 3, &call);
  EXPECT_THAT(ListValues<int>(call.info().at("key")), ElementsAre(3));
  SetInfoField("key", 4, &call);
  EXPECT_THAT(ListValues<int>(call.info().at("key")), ElementsAre(4));
}

}  // namespace nucleus
