//   Copyright 2023, University of Freiburg,
//   Chair of Algorithms and Data Structures.
//   Author: Robin Textor-Falconi <textorr@informatik.uni-freiburg.de>

#include <absl/cleanup/cleanup.h>
#include <gmock/gmock.h>

#include "util/CancellationHandle.h"
#include "util/GTestHelpers.h"
#include "util/jthread.h"

using ad_utility::CancellationException;
using ad_utility::CancellationHandle;
using enum ad_utility::CancellationState;
using enum ad_utility::detail::CancellationMode;
using ::testing::AllOf;
using ::testing::ContainsRegex;
using ::testing::HasSubstr;

using namespace std::chrono_literals;

template <typename CancellationHandle>
struct CancellationHandleFixture : public ::testing::Test {
  CancellationHandle handle_;
};
using WithAndWithoutWatchDog =
    ::testing::Types<CancellationHandle<ENABLED>,
                     CancellationHandle<NO_WATCH_DOG>>;
TYPED_TEST_SUITE(CancellationHandleFixture, WithAndWithoutWatchDog);

// _____________________________________________________________________________

TEST(CancellationHandle, verifyConstructorMessageIsPassed) {
  auto message = "Message";
  CancellationException exception{message};
  EXPECT_STREQ(message, exception.what());
}

// _____________________________________________________________________________

TEST(CancellationHandle, verifyConstructorDoesNotAcceptNoReason) {
  EXPECT_THROW(CancellationException exception(NOT_CANCELLED, ""),
               ad_utility::Exception);
}

// _____________________________________________________________________________

TYPED_TEST(CancellationHandleFixture, verifyNotCancelledByDefault) {
  auto& handle = this->handle_;

  EXPECT_FALSE(handle.isCancelled());
  EXPECT_NO_THROW(handle.throwIfCancelled(""));
  EXPECT_NO_THROW(handle.throwIfCancelled([]() { return ""; }));
}

// _____________________________________________________________________________

TYPED_TEST(CancellationHandleFixture, verifyCancelWithWrongReasonThrows) {
  auto& handle = this->handle_;
  EXPECT_THROW(handle.cancel(NOT_CANCELLED), ad_utility::Exception);
}

// _____________________________________________________________________________

auto detail = "Some Detail";

TYPED_TEST(CancellationHandleFixture, verifyTimeoutCancellationWorks) {
  auto& handle = this->handle_;

  handle.cancel(TIMEOUT);

  auto timeoutMessageMatcher = AllOf(HasSubstr(detail), HasSubstr("timeout"));
  EXPECT_TRUE(handle.isCancelled());
  AD_EXPECT_THROW_WITH_MESSAGE_AND_TYPE(handle.throwIfCancelled(detail),
                                        timeoutMessageMatcher,
                                        CancellationException);
  AD_EXPECT_THROW_WITH_MESSAGE_AND_TYPE(
      handle.throwIfCancelled([]() { return detail; }), timeoutMessageMatcher,
      CancellationException);
}

// _____________________________________________________________________________

TYPED_TEST(CancellationHandleFixture, verifyManualCancellationWorks) {
  auto& handle = this->handle_;

  handle.cancel(MANUAL);

  auto cancellationMessageMatcher =
      AllOf(HasSubstr(detail), HasSubstr("manual cancellation"));
  EXPECT_TRUE(handle.isCancelled());
  AD_EXPECT_THROW_WITH_MESSAGE_AND_TYPE(handle.throwIfCancelled(detail),
                                        cancellationMessageMatcher,
                                        CancellationException);
  AD_EXPECT_THROW_WITH_MESSAGE_AND_TYPE(
      handle.throwIfCancelled([]() { return detail; }),
      cancellationMessageMatcher, CancellationException);
}

// _____________________________________________________________________________

TYPED_TEST(CancellationHandleFixture,
           verifyCancellationWorksWithMultipleThreads) {
  auto& handle = this->handle_;

  ad_utility::JThread thread{[&]() {
    std::this_thread::sleep_for(5ms);
    handle.cancel(TIMEOUT);
  }};

  EXPECT_THROW(
      {
        auto end = std::chrono::steady_clock::now() + 100ms;
        while (std::chrono::steady_clock::now() < end) {
          handle.throwIfCancelled("Some Detail");
        }
      },
      CancellationException);
  EXPECT_TRUE(handle.isCancelled());
}

// _____________________________________________________________________________

TEST(CancellationHandle, ensureObjectLifetimeIsValidWithoutWatchDogStarted) {
  EXPECT_NO_THROW(CancellationHandle<ENABLED>{});
}

// _____________________________________________________________________________

namespace ad_utility {

TEST(CancellationHandle, verifyWatchDogDoesChangeState) {
#ifdef __APPLE__
  GTEST_SKIP_("sleep_for is unreliable for macos builds");
#endif
  CancellationHandle<ENABLED> handle;

  EXPECT_EQ(handle.cancellationState_, NOT_CANCELLED);
  handle.startWatchDog();

  // Give thread some time to start
  std::this_thread::sleep_for(10ms);
  EXPECT_EQ(handle.cancellationState_, WAITING_FOR_CHECK);

  std::this_thread::sleep_for(DESIRED_CANCELLATION_CHECK_INTERVAL);
  EXPECT_EQ(handle.cancellationState_, CHECK_WINDOW_MISSED);
}

// _____________________________________________________________________________

TEST(CancellationHandle, verifyWatchDogDoesNotChangeStateAfterCancel) {
#ifdef __APPLE__
  GTEST_SKIP_("sleep_for is unreliable for macos builds");
#endif
  CancellationHandle<ENABLED> handle;
  handle.startWatchDog();

  // Give thread some time to start
  std::this_thread::sleep_for(10ms);

  handle.cancellationState_ = MANUAL;
  std::this_thread::sleep_for(DESIRED_CANCELLATION_CHECK_INTERVAL);
  EXPECT_EQ(handle.cancellationState_, MANUAL);

  handle.cancellationState_ = TIMEOUT;
  std::this_thread::sleep_for(DESIRED_CANCELLATION_CHECK_INTERVAL);
  EXPECT_EQ(handle.cancellationState_, TIMEOUT);
}

// _____________________________________________________________________________

TEST(CancellationHandle, ensureDestructorReturnsFastWithActiveWatchDog) {
  std::chrono::steady_clock::time_point start;
  {
    CancellationHandle<ENABLED> handle;
    handle.startWatchDog();
    start = std::chrono::steady_clock::now();
  }
  auto duration = std::chrono::steady_clock::now() - start;
  // Ensure we don't need to wait for the entire interval to finish.
  EXPECT_LT(duration, DESIRED_CANCELLATION_CHECK_INTERVAL);
}

// _____________________________________________________________________________

TEST(CancellationHandle, verifyResetWatchDogStateDoesProperlyResetState) {
  CancellationHandle<ENABLED> handle;

  handle.cancellationState_ = NOT_CANCELLED;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, NOT_CANCELLED);

  handle.cancellationState_ = WAITING_FOR_CHECK;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, NOT_CANCELLED);

  handle.cancellationState_ = CHECK_WINDOW_MISSED;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, NOT_CANCELLED);

  handle.cancellationState_ = MANUAL;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, MANUAL);

  handle.cancellationState_ = TIMEOUT;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, TIMEOUT);
}

// _____________________________________________________________________________

TEST(CancellationHandle, verifyResetWatchDogStateIsNoOpWithoutWatchDog) {
  CancellationHandle<NO_WATCH_DOG> handle;

  handle.cancellationState_ = NOT_CANCELLED;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, NOT_CANCELLED);

  handle.cancellationState_ = WAITING_FOR_CHECK;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, WAITING_FOR_CHECK);

  handle.cancellationState_ = CHECK_WINDOW_MISSED;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, CHECK_WINDOW_MISSED);

  handle.cancellationState_ = MANUAL;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, MANUAL);

  handle.cancellationState_ = TIMEOUT;
  handle.resetWatchDogState();
  EXPECT_EQ(handle.cancellationState_, TIMEOUT);
}

// _____________________________________________________________________________

TEST(CancellationHandle, verifyCheckDoesPleaseWatchDog) {
  CancellationHandle<ENABLED> handle;

  handle.cancellationState_ = WAITING_FOR_CHECK;
  EXPECT_NO_THROW(handle.throwIfCancelled(""));
  EXPECT_EQ(handle.cancellationState_, NOT_CANCELLED);

  handle.cancellationState_ = CHECK_WINDOW_MISSED;
  EXPECT_NO_THROW(handle.throwIfCancelled(""));
  EXPECT_EQ(handle.cancellationState_, NOT_CANCELLED);
}

// _____________________________________________________________________________

TEST(CancellationHandle, verifyCheckDoesNotOverrideCancelledState) {
  CancellationHandle<ENABLED> handle;

  handle.cancellationState_ = MANUAL;
  EXPECT_THROW(handle.throwIfCancelled(""), CancellationException);
  EXPECT_EQ(handle.cancellationState_, MANUAL);

  handle.cancellationState_ = TIMEOUT;
  EXPECT_THROW(handle.throwIfCancelled(""), CancellationException);
  EXPECT_EQ(handle.cancellationState_, TIMEOUT);
}

// _____________________________________________________________________________

TEST(CancellationHandle, verifyCheckAfterDeadlineMissDoesReportProperly) {
  auto& choice = ad_utility::LogstreamChoice::get();
  CancellationHandle<ENABLED> handle;

  auto& originalOStream = choice.getStream();
  absl::Cleanup cleanup{[&]() { choice.setStream(&originalOStream); }};

  std::ostringstream testStream;
  choice.setStream(&testStream);

  handle.startTimeoutWindow_ = std::chrono::steady_clock::now();
  handle.cancellationState_ = CHECK_WINDOW_MISSED;
  EXPECT_NO_THROW(handle.throwIfCancelled("my-detail"));
  EXPECT_EQ(handle.cancellationState_, NOT_CANCELLED);

  EXPECT_THAT(
      std::move(testStream).str(),
      AllOf(HasSubstr("my-detail"),
            HasSubstr(ParseableDuration{DESIRED_CANCELLATION_CHECK_INTERVAL}
                          .toString()),
            // Check for small miss window
            ContainsRegex("by [0-9]ms")));
}

// _____________________________________________________________________________

TEST(CancellationHandle, expectDisabledHandleIsAlwaysFalse) {
  CancellationHandle<DISABLED> handle;

  EXPECT_FALSE(handle.isCancelled());
  EXPECT_NO_THROW(handle.throwIfCancelled("Abc"));
}

consteval bool isMemberFunction([[maybe_unused]] auto funcPtr) {
  return std::is_member_function_pointer_v<decltype(funcPtr)>;
}

// Make sure member functions still exist when no watch dog functionality
// is available to make the code simpler. In this case the functions should
// be no-op.
static_assert(
    isMemberFunction(&CancellationHandle<NO_WATCH_DOG>::startWatchDog));
static_assert(
    isMemberFunction(&CancellationHandle<NO_WATCH_DOG>::resetWatchDogState));
static_assert(isMemberFunction(&CancellationHandle<DISABLED>::startWatchDog));
static_assert(
    isMemberFunction(&CancellationHandle<DISABLED>::resetWatchDogState));
static_assert(isMemberFunction(&CancellationHandle<DISABLED>::cancel));
static_assert(isMemberFunction(&CancellationHandle<DISABLED>::isCancelled));
// Ideally we'd add a static assertion for throwIfCancelled here too, but
// because the function is overloaded, we can't get a function pointer for it.

}  // namespace ad_utility
