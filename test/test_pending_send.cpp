#include "../src/daemon/notification_keys.hpp"
#include "../src/gtk/pending_send.hpp"

#include <gtest/gtest.h>

using tether::ui::PendingSends;

TEST(PendingSends, ResultsBelongToTheirConversationAndOperation) {
    PendingSends sends;
    sends.put({"attempt-a", "tel:+15551234567", "message A"});
    sends.put({"attempt-b", "tel:+15559876543", "message B"});
    EXPECT_EQ(sends.for_result("tel:+15551234567", "attempt-a")->body, "message A");
    EXPECT_EQ(sends.for_result("tel:+15559876543", "attempt-b")->body, "message B");
    EXPECT_EQ(sends.for_result("tel:+15559876543", "attempt-a"), nullptr);
    EXPECT_EQ(sends.for_result("tel:+15551234567", ""), nullptr);
    sends.put({"retry-a", "tel:+15551234567", "retry"});
    EXPECT_EQ(sends.for_result("tel:+15551234567", "attempt-a"), nullptr);
}

TEST(PendingSends, FailedBubbleSurvivesUnrelatedMessagesAndThreadChanges) {
    PendingSends sends;
    sends.put({"attempt", "tel:+15551234567", "keep me", {}, "failed"});
    EXPECT_FALSE(sends.reconcile("tel:+15551234567", "incoming"));
    EXPECT_EQ(sends.for_thread("tel:+15559876543"), nullptr);
    EXPECT_EQ(sends.for_thread("tel:+15551234567")->failure, "failed");
    EXPECT_EQ(sends.for_thread("tel:+15551234567")->body, "keep me");
}

TEST(PendingSends, OnlyConfirmedHandleReplacesSyntheticBubble) {
    PendingSends sends;
    sends.put({"attempt", "tel:+15551234567", "body", "sent-handle", {}, true});
    EXPECT_FALSE(sends.reconcile("tel:+15559876543", "sent-handle"));
    EXPECT_FALSE(sends.reconcile("tel:+15551234567", "unrelated-handle"));
    EXPECT_TRUE(sends.reconcile("tel:+15551234567", "sent-handle"));
    EXPECT_EQ(sends.for_thread("tel:+15551234567"), nullptr);
}

TEST(NotificationKeys, WithdrawClosesOnlyTheLastMemberOfAGroup) {
    int grouped_popup = 1;
    int other_popup = 2;
    std::unordered_map<std::string, int*> keys{
        {"first", &grouped_popup}, {"second", &grouped_popup}, {"other", &other_popup}};
    EXPECT_EQ(tether::detach_notification_key(keys, "first"), nullptr);
    EXPECT_EQ(keys.at("second"), &grouped_popup);
    EXPECT_EQ(tether::detach_notification_key(keys, "first"), nullptr);
    EXPECT_EQ(tether::detach_notification_key(keys, "second"), &grouped_popup);
    EXPECT_EQ(keys.at("other"), &other_popup);
    EXPECT_EQ(tether::detach_notification_key(keys, "other"), &other_popup);
    EXPECT_TRUE(keys.empty());
}

TEST(PendingSends, PhoneReplacementMustBeNewOutgoingMatchingBodyAndTime) {
    PendingSends sends;
    sends.put({"attempt", "tel:+15551234567", "two\nlines", "local-echo", {}, true, 1000, {"old"}});
    EXPECT_FALSE(sends.reconcile("tel:+15551234567", "old", "two lines", true, 1000));
    EXPECT_FALSE(sends.reconcile("tel:+15551234567", "incoming", "two lines", false, 1000));
    EXPECT_FALSE(sends.reconcile("tel:+15551234567", "different", "other text", true, 1000));
    EXPECT_FALSE(sends.reconcile("tel:+15551234567", "too-late", "two lines", true, 1400));
    EXPECT_TRUE(sends.reconcile("tel:+15551234567", "phone-copy", "two lines", true, 1001));
}
