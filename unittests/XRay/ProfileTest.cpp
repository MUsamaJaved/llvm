//===- ProfileTest.cpp - XRay Profile unit tests ----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
#include "llvm/XRay/Profile.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include <numeric>

namespace llvm {
namespace xray {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Not;
using ::testing::Pair;
using ::testing::UnorderedElementsAre;

TEST(ProfileTest, CreateProfile) { Profile P; }

TEST(ProfileTest, InternPath) {
  Profile P;
  auto Path0 = P.internPath({3, 2, 1});
  auto Path1 = P.internPath({3, 2, 1});
  auto Path2 = P.internPath({2, 1});
  EXPECT_THAT(Path0, Eq(Path1));
  EXPECT_THAT(Path0, Not(Eq(Path2)));
}

TEST(ProfileTest, ExpandPath) {
  Profile P;
  auto PathID = P.internPath({3, 2, 1});
  auto PathOrError = P.expandPath(PathID);
  if (!PathOrError)
    FAIL() << "Error: " << PathOrError.takeError();
  EXPECT_THAT(PathOrError.get(), ElementsAre(3, 2, 1));
}

TEST(ProfileTest, AddBlocks) {
  Profile P;
  // Expect an error on adding empty blocks.
  EXPECT_TRUE(errorToBool(P.addBlock({})));

  // Thread blocks may not be empty.
  EXPECT_TRUE(errorToBool(P.addBlock({1, {}})));

  // Thread blocks with data must succeed.
  EXPECT_FALSE(errorToBool(P.addBlock(
      Profile::Block{Profile::ThreadID{1},
                     {
                         {P.internPath({2, 1}), Profile::Data{1, 1000}},
                         {P.internPath({3, 2, 1}), Profile::Data{10, 100}},
                     }})));
}

TEST(ProfileTest, MergeProfilesByThread) {
  Profile P0, P1;

  // Set up the blocks for two different threads in P0.
  EXPECT_FALSE(errorToBool(P0.addBlock(
      Profile::Block{Profile::ThreadID{1},
                     {{P0.internPath({2, 1}), Profile::Data{1, 1000}},
                      {P0.internPath({4, 1}), Profile::Data{1, 1000}}}})));
  EXPECT_FALSE(errorToBool(P0.addBlock(
      Profile::Block{Profile::ThreadID{2},
                     {{P0.internPath({3, 1}), Profile::Data{1, 1000}}}})));

  // Set up the blocks for two different threads in P1.
  EXPECT_FALSE(errorToBool(P1.addBlock(
      Profile::Block{Profile::ThreadID{1},
                     {{P1.internPath({2, 1}), Profile::Data{1, 1000}}}})));
  EXPECT_FALSE(errorToBool(P1.addBlock(
      Profile::Block{Profile::ThreadID{2},
                     {{P1.internPath({3, 1}), Profile::Data{1, 1000}},
                      {P1.internPath({4, 1}), Profile::Data{1, 1000}}}})));

  Profile Merged = mergeProfilesByThread(P0, P1);
  EXPECT_THAT(
      Merged,
      UnorderedElementsAre(
          // We want to see two threads after the merge.
          AllOf(Field(&Profile::Block::Thread, Eq(Profile::ThreadID{1})),
                Field(&Profile::Block::PathData,
                      UnorderedElementsAre(
                          Pair(Merged.internPath({2, 1}),
                               AllOf(Field(&Profile::Data::CallCount, Eq(2u)),
                                     Field(&Profile::Data::CumulativeLocalTime,
                                           Eq(2000u)))),
                          Pair(Merged.internPath({4, 1}),
                               AllOf(Field(&Profile::Data::CallCount, Eq(1u)),
                                     Field(&Profile::Data::CumulativeLocalTime,
                                           Eq(1000u))))))),
          AllOf(Field(&Profile::Block::Thread, Eq(Profile::ThreadID{2})),
                Field(&Profile::Block::PathData,
                      UnorderedElementsAre(
                          Pair(Merged.internPath({3, 1}),
                               AllOf(Field(&Profile::Data::CallCount, Eq(2u)),
                                     Field(&Profile::Data::CumulativeLocalTime,
                                           Eq(2000u)))),
                          Pair(Merged.internPath({4, 1}),
                               AllOf(Field(&Profile::Data::CallCount, Eq(1u)),
                                     Field(&Profile::Data::CumulativeLocalTime,
                                           Eq(1000u)))))))));
}

TEST(ProfileTest, MergeProfilesByStack) {
  Profile P0, P1;
  EXPECT_FALSE(errorToBool(P0.addBlock(
      Profile::Block{Profile::ThreadID{1},
                     {{P0.internPath({2, 1}), Profile::Data{1, 1000}}}})));
  EXPECT_FALSE(errorToBool(P1.addBlock(
      Profile::Block{Profile::ThreadID{2},
                     {{P1.internPath({2, 1}), Profile::Data{1, 1000}}}})));

  Profile Merged = mergeProfilesByStack(P0, P1);
  EXPECT_THAT(Merged,
              ElementsAre(AllOf(
                  // We expect that we lose the ThreadID dimension in this
                  // algorithm.
                  Field(&Profile::Block::Thread, Eq(Profile::ThreadID{0})),
                  Field(&Profile::Block::PathData,
                        ElementsAre(Pair(
                            Merged.internPath({2, 1}),
                            AllOf(Field(&Profile::Data::CallCount, Eq(2u)),
                                  Field(&Profile::Data::CumulativeLocalTime,
                                        Eq(2000u)))))))));
}

TEST(ProfileTest, MergeProfilesByStackAccumulate) {
  std::vector<Profile> Profiles(3);
  EXPECT_FALSE(errorToBool(Profiles[0].addBlock(Profile::Block{
      Profile::ThreadID{1},
      {{Profiles[0].internPath({2, 1}), Profile::Data{1, 1000}}}})));
  EXPECT_FALSE(errorToBool(Profiles[1].addBlock(Profile::Block{
      Profile::ThreadID{2},
      {{Profiles[1].internPath({2, 1}), Profile::Data{1, 1000}}}})));
  EXPECT_FALSE(errorToBool(Profiles[2].addBlock(Profile::Block{
      Profile::ThreadID{3},
      {{Profiles[2].internPath({2, 1}), Profile::Data{1, 1000}}}})));
  Profile Merged = std::accumulate(Profiles.begin(), Profiles.end(), Profile(),
                                   mergeProfilesByStack);
  EXPECT_THAT(Merged,
              ElementsAre(AllOf(
                  // We expect that we lose the ThreadID dimension in this
                  // algorithm.
                  Field(&Profile::Block::Thread, Eq(Profile::ThreadID{0})),
                  Field(&Profile::Block::PathData,
                        ElementsAre(Pair(
                            Merged.internPath({2, 1}),
                            AllOf(Field(&Profile::Data::CallCount, Eq(3u)),
                                  Field(&Profile::Data::CumulativeLocalTime,
                                        Eq(3000u)))))))));
}

TEST(ProfileTest, MergeProfilesByThreadAccumulate) {
  std::vector<Profile> Profiles(2);

  // Set up the blocks for two different threads in Profiles[0].
  EXPECT_FALSE(errorToBool(Profiles[0].addBlock(Profile::Block{
      Profile::ThreadID{1},
      {{Profiles[0].internPath({2, 1}), Profile::Data{1, 1000}},
       {Profiles[0].internPath({4, 1}), Profile::Data{1, 1000}}}})));
  EXPECT_FALSE(errorToBool(Profiles[0].addBlock(Profile::Block{
      Profile::ThreadID{2},
      {{Profiles[0].internPath({3, 1}), Profile::Data{1, 1000}}}})));

  // Set up the blocks for two different threads in Profiles[1].
  EXPECT_FALSE(errorToBool(Profiles[1].addBlock(Profile::Block{
      Profile::ThreadID{1},
      {{Profiles[1].internPath({2, 1}), Profile::Data{1, 1000}}}})));
  EXPECT_FALSE(errorToBool(Profiles[1].addBlock(Profile::Block{
      Profile::ThreadID{2},
      {{Profiles[1].internPath({3, 1}), Profile::Data{1, 1000}},
       {Profiles[1].internPath({4, 1}), Profile::Data{1, 1000}}}})));

  Profile Merged = std::accumulate(Profiles.begin(), Profiles.end(), Profile(),
                                   mergeProfilesByThread);
  EXPECT_THAT(
      Merged,
      UnorderedElementsAre(
          // We want to see two threads after the merge.
          AllOf(Field(&Profile::Block::Thread, Eq(Profile::ThreadID{1})),
                Field(&Profile::Block::PathData,
                      UnorderedElementsAre(
                          Pair(Merged.internPath({2, 1}),
                               AllOf(Field(&Profile::Data::CallCount, Eq(2u)),
                                     Field(&Profile::Data::CumulativeLocalTime,
                                           Eq(2000u)))),
                          Pair(Merged.internPath({4, 1}),
                               AllOf(Field(&Profile::Data::CallCount, Eq(1u)),
                                     Field(&Profile::Data::CumulativeLocalTime,
                                           Eq(1000u))))))),
          AllOf(Field(&Profile::Block::Thread, Eq(Profile::ThreadID{2})),
                Field(&Profile::Block::PathData,
                      UnorderedElementsAre(
                          Pair(Merged.internPath({3, 1}),
                               AllOf(Field(&Profile::Data::CallCount, Eq(2u)),
                                     Field(&Profile::Data::CumulativeLocalTime,
                                           Eq(2000u)))),
                          Pair(Merged.internPath({4, 1}),
                               AllOf(Field(&Profile::Data::CallCount, Eq(1u)),
                                     Field(&Profile::Data::CumulativeLocalTime,
                                           Eq(1000u)))))))));
}
// FIXME: Add a test creating a Trace and generating a Profile
// FIXME: Add tests for ranking/sorting profile blocks by dimension

} // namespace
} // namespace xray
} // namespace llvm
