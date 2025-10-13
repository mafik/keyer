// Include the implementation (Python.h mock is in the same directory)
// Include the core Fingers logic (no Python dependencies)
#include "fingers.cpp"

#include <gtest/gtest.h>

// Test fixture for Fingers transition tests
class FingersTransitionTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Default position: fingers not pressed, non-thumb fingers over row 0,
    // thumb over row 1
  }
};

TEST_F(FingersTransitionTest, DefaultPosition) {
  Fingers fingers{};
  EXPECT_EQ(fingers.pressed, 0u);
  EXPECT_EQ(fingers.rows[0], MASK_NON_THUMB);
  EXPECT_EQ(fingers.rows[1], MASK_THUMB);
  EXPECT_EQ(fingers.rows[2], 0);
  EXPECT_EQ(fingers.finger_to_row[0], 1);
  EXPECT_EQ(fingers.finger_to_row[1], 0);
}

TEST_F(FingersTransitionTest, NastyRelease) {
  Fingers current = Fingers::FromChord("1100");
  Fingers target = Fingers::FromChord("1000");
  uint32_t cost = current.transition_to(target);

  EXPECT_EQ(cost, FINGER_PRESS_COST_MS[0][0]);
  EXPECT_EQ(current.pressed, target.pressed);
}

TEST_F(FingersTransitionTest, FingerMove) {
  Fingers current = Fingers::FromChord("1100");
  Fingers target = Fingers::FromChord("2100");
  uint32_t cost = current.transition_to(target);

  EXPECT_EQ(cost, FINGER_PRESS_COST_MS[0][1] + FINGER_TRAVEL_COST_MS[0]);
  EXPECT_EQ(current.pressed, target.pressed);
}

TEST_F(FingersTransitionTest, FingerSwap) {
  Fingers current = Fingers::FromChord("2100");
  Fingers target = Fingers::FromChord("2010");
  uint32_t cost = current.transition_to(target);

  EXPECT_EQ(cost, FINGER_PRESS_COST_MS[2][0]);
  EXPECT_EQ(current.pressed, target.pressed);
}

TEST_F(FingersTransitionTest, FingerAdd) {
  Fingers current = Fingers::FromChord("2100");
  Fingers target = Fingers::FromChord("2110");
  uint32_t cost = current.transition_to(target);

  EXPECT_EQ(cost, FINGER_PRESS_COST_MS[0][1] + FINGER_PRESS_COST_MS[2][0]);
  EXPECT_EQ(current.pressed, target.pressed);
}

TEST_F(FingersTransitionTest, SimpleMove) {
  Fingers current = Fingers::FromChord("2000");
  Fingers target = Fingers::FromChord("1000");
  uint32_t cost = current.transition_to(target);

  uint32_t expected_cost =
      FINGER_TRAVEL_COST_MS[0] * 1 + FINGER_PRESS_COST_MS[0][0];
  EXPECT_EQ(cost, expected_cost);
  EXPECT_EQ(current.get(0), 0);
  EXPECT_TRUE(current.is_pressed(0));
}

TEST_F(FingersTransitionTest, MultipleFingersMoved) {
  Fingers current = Fingers::FromChord("1100");
  Fingers target = Fingers::FromChord("2200");
  uint32_t cost = current.transition_to(target);

  uint32_t expected_cost =
      (FINGER_TRAVEL_COST_MS[0] + FINGER_PRESS_COST_MS[0][1]) +
      (FINGER_TRAVEL_COST_MS[1] + FINGER_PRESS_COST_MS[1][1]);
  EXPECT_EQ(cost, expected_cost);
  EXPECT_EQ(current.get(0), 1);
  EXPECT_EQ(current.get(1), 1);
}

TEST_F(FingersTransitionTest, RePressThumb) {
  Fingers current = Fingers::FromChord("2100");
  Fingers target = Fingers::FromChord("2100");
  uint32_t cost = current.transition_to(target);
  uint32_t expected_cost = FINGER_PRESS_COST_MS[0][1];
  EXPECT_EQ(cost, expected_cost);
}

TEST_F(FingersTransitionTest, RePressIndex) {
  Fingers current = Fingers::FromChord("0101");
  Fingers target = Fingers::FromChord("2111");
  uint32_t cost = current.transition_to(target);
  uint32_t expected_cost = FINGER_PRESS_COST_MS[1][0] +
                           FINGER_PRESS_COST_MS[0][1] +
                           FINGER_PRESS_COST_MS[2][0];
  EXPECT_EQ(cost, expected_cost);
}

TEST_F(FingersTransitionTest, NoFingersInitiallyPressed) {
  Fingers current = {};
  Fingers target = Fingers::FromChord("0100");
  uint32_t cost = current.transition_to(target);
  uint32_t expected_cost = FINGER_PRESS_COST_MS[1][0];
  EXPECT_EQ(cost, expected_cost);
  EXPECT_TRUE(current.is_pressed(1));
}

TEST_F(FingersTransitionTest, Test) {
  Fingers current = Fingers::FromChord("2001");
  Fingers target = Fingers::FromChord("2011");
  uint32_t cost = current.transition_to(target);
  uint32_t expected_cost = 0;
  EXPECT_EQ(cost, expected_cost);
  EXPECT_TRUE(current.is_pressed(1));
}

TEST_F(FingersTransitionTest, LongDistanceTravel) {
  Fingers current = Fingers::FromChord("1000");
  Fingers target = Fingers::FromChord("3000");
  uint32_t cost = current.transition_to(target);
  uint32_t expected_cost =
      FINGER_TRAVEL_COST_MS[0] * 2 + FINGER_PRESS_COST_MS[0][2];
  EXPECT_EQ(cost, expected_cost);
  EXPECT_EQ(current.get(0), 2);
}

// Some release, some move, some press
TEST_F(FingersTransitionTest, MixedScenario) {
  Fingers current = Fingers::FromChord("1200");
  Fingers target = Fingers::FromChord("0120");
  uint32_t cost = current.transition_to(target);

  uint32_t expected_cost =
      FINGER_TRAVEL_COST_MS[1] * 1 + FINGER_TRAVEL_COST_MS[2] * 1 +
      FINGER_PRESS_COST_MS[1][0] + FINGER_PRESS_COST_MS[2][1];
  EXPECT_EQ(cost, expected_cost);
  EXPECT_FALSE(current.is_pressed(0));
  EXPECT_TRUE(current.is_pressed(1));
  EXPECT_TRUE(current.is_pressed(2));
}

// Test state consistency after transition
TEST_F(FingersTransitionTest, StateConsistency) {
  Fingers current = {};
  Fingers target = Fingers::FromChord("3210");

  current.transition_to(target);

  for (int i = 0; i < NUM_FINGERS; i++) {
    EXPECT_EQ(current.is_pressed(i), target.is_pressed(i));
  }

  for (int i = 0; i < NUM_FINGERS; i++) {
    if (target.is_pressed(i)) {
      EXPECT_EQ(current.get(i), target.get(i));
    }
  }
}

TEST_F(FingersTransitionTest, AllFingersPressedSimultaneously) {
  Fingers current = {};
  Fingers target = Fingers::FromChord("2111");

  uint32_t cost = current.transition_to(target);

  // Cost of the initial press includes the cost of all buttons
  uint32_t expected_cost = 0;
  expected_cost += FINGER_PRESS_COST_MS[0][1];
  for (int i = 1; i < NUM_FINGERS; i++) {
    expected_cost += FINGER_PRESS_COST_MS[i][0];
  }

  EXPECT_EQ(cost, expected_cost);
  EXPECT_EQ(current.pressed, MASK_ALL);

  // Test re-press
  uint32_t re_press_cost = current.transition_to(target);
  EXPECT_EQ(re_press_cost, 40);
}

TEST_F(FingersTransitionTest, FromChordParsing) {
  Fingers fingers = Fingers::FromChord("1230");

  EXPECT_TRUE(fingers.is_pressed(0));
  EXPECT_TRUE(fingers.is_pressed(1));
  EXPECT_TRUE(fingers.is_pressed(2));
  EXPECT_FALSE(fingers.is_pressed(3));

  EXPECT_EQ(fingers.get(0), 0);
  EXPECT_EQ(fingers.get(1), 1);
  EXPECT_EQ(fingers.get(2), 2);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
