/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <list>
#include <memory>

#include "webrtc/modules/pacing/packet_router.h"
#include "webrtc/modules/rtp_rtcp/include/rtp_rtcp.h"
#include "webrtc/modules/rtp_rtcp/mocks/mock_rtp_rtcp.h"
#include "webrtc/modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "webrtc/rtc_base/checks.h"
#include "webrtc/rtc_base/fakeclock.h"
#include "webrtc/test/gmock.h"
#include "webrtc/test/gtest.h"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Field;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::SaveArg;

namespace webrtc {

// TODO(eladalon): Restructure and/or replace the existing monolithic tests
// (only some of the test are monolithic) according to the new
// guidelines - small tests for one thing at a time.
// (I'm not removing any tests during CL, so as to demonstrate no regressions.)

namespace {
constexpr int kProbeMinProbes = 5;
constexpr int kProbeMinBytes = 1000;

class MockRtpRtcpWithRembTracking : public MockRtpRtcp {
 public:
  MockRtpRtcpWithRembTracking() {
    ON_CALL(*this, SetREMBStatus(_)).WillByDefault(SaveArg<0>(&remb_));
    ON_CALL(*this, REMB()).WillByDefault(ReturnPointee(&remb_));
  }

 private:
  bool remb_ = false;
};
}  // namespace

TEST(PacketRouterTest, Sanity_NoModuleRegistered_TimeToSendPacket) {
  PacketRouter packet_router;

  constexpr uint16_t ssrc = 1234;
  constexpr uint16_t sequence_number = 17;
  constexpr uint64_t timestamp = 7890;
  constexpr bool retransmission = false;
  const PacedPacketInfo paced_info(1, kProbeMinProbes, kProbeMinBytes);

  // TODO(eladalon): TimeToSendPacket() returning true when nothing was
  // sent, because no modules were registered, is sub-optimal.
  // https://bugs.chromium.org/p/webrtc/issues/detail?id=8052
  EXPECT_TRUE(packet_router.TimeToSendPacket(ssrc, sequence_number, timestamp,
                                             retransmission, paced_info));
}

TEST(PacketRouterTest, Sanity_NoModuleRegistered_TimeToSendPadding) {
  PacketRouter packet_router;

  constexpr size_t bytes = 300;
  const PacedPacketInfo paced_info(1, kProbeMinProbes, kProbeMinBytes);

  EXPECT_EQ(packet_router.TimeToSendPadding(bytes, paced_info), 0u);
}

TEST(PacketRouterTest, Sanity_NoModuleRegistered_OnReceiveBitrateChanged) {
  PacketRouter packet_router;

  const std::vector<uint32_t> ssrcs = {1, 2, 3};
  constexpr uint32_t bitrate_bps = 10000;

  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_bps);
}

TEST(PacketRouterTest, Sanity_NoModuleRegistered_SendRemb) {
  PacketRouter packet_router;

  const std::vector<uint32_t> ssrcs = {1, 2, 3};
  constexpr uint32_t bitrate_bps = 10000;

  EXPECT_FALSE(packet_router.SendRemb(bitrate_bps, ssrcs));
}

TEST(PacketRouterTest, Sanity_NoModuleRegistered_SendTransportFeedback) {
  PacketRouter packet_router;

  rtcp::TransportFeedback feedback;

  EXPECT_FALSE(packet_router.SendTransportFeedback(&feedback));
}

TEST(PacketRouterTest, TimeToSendPacket) {
  PacketRouter packet_router;
  NiceMock<MockRtpRtcp> rtp_1;
  NiceMock<MockRtpRtcp> rtp_2;

  packet_router.AddSendRtpModule(&rtp_1, false);
  packet_router.AddSendRtpModule(&rtp_2, false);

  const uint16_t kSsrc1 = 1234;
  uint16_t sequence_number = 17;
  uint64_t timestamp = 7890;
  bool retransmission = false;

  // Send on the first module by letting rtp_1 be sending with correct ssrc.
  EXPECT_CALL(rtp_1, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_1, SSRC()).Times(1).WillOnce(Return(kSsrc1));
  EXPECT_CALL(rtp_1, TimeToSendPacket(
                         kSsrc1, sequence_number, timestamp, retransmission,
                         Field(&PacedPacketInfo::probe_cluster_id, 1)))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_CALL(rtp_2, TimeToSendPacket(_, _, _, _, _)).Times(0);
  EXPECT_TRUE(packet_router.TimeToSendPacket(
      kSsrc1, sequence_number, timestamp, retransmission,
      PacedPacketInfo(1, kProbeMinProbes, kProbeMinBytes)));

  // Send on the second module by letting rtp_2 be sending, but not rtp_1.
  ++sequence_number;
  timestamp += 30;
  retransmission = true;
  const uint16_t kSsrc2 = 4567;
  EXPECT_CALL(rtp_1, SendingMedia()).Times(1).WillOnce(Return(false));
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2, SSRC()).Times(1).WillOnce(Return(kSsrc2));
  EXPECT_CALL(rtp_1, TimeToSendPacket(_, _, _, _, _)).Times(0);
  EXPECT_CALL(rtp_2, TimeToSendPacket(
                         kSsrc2, sequence_number, timestamp, retransmission,
                         Field(&PacedPacketInfo::probe_cluster_id, 2)))
      .Times(1)
      .WillOnce(Return(true));
  EXPECT_TRUE(packet_router.TimeToSendPacket(
      kSsrc2, sequence_number, timestamp, retransmission,
      PacedPacketInfo(2, kProbeMinProbes, kProbeMinBytes)));

  // No module is sending, hence no packet should be sent.
  EXPECT_CALL(rtp_1, SendingMedia()).Times(1).WillOnce(Return(false));
  EXPECT_CALL(rtp_1, TimeToSendPacket(_, _, _, _, _)).Times(0);
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(false));
  EXPECT_CALL(rtp_2, TimeToSendPacket(_, _, _, _, _)).Times(0);
  EXPECT_TRUE(packet_router.TimeToSendPacket(
      kSsrc1, sequence_number, timestamp, retransmission,
      PacedPacketInfo(1, kProbeMinProbes, kProbeMinBytes)));

  // Add a packet with incorrect ssrc and test it's dropped in the router.
  EXPECT_CALL(rtp_1, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_1, SSRC()).Times(1).WillOnce(Return(kSsrc1));
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2, SSRC()).Times(1).WillOnce(Return(kSsrc2));
  EXPECT_CALL(rtp_1, TimeToSendPacket(_, _, _, _, _)).Times(0);
  EXPECT_CALL(rtp_2, TimeToSendPacket(_, _, _, _, _)).Times(0);
  EXPECT_TRUE(packet_router.TimeToSendPacket(
      kSsrc1 + kSsrc2, sequence_number, timestamp, retransmission,
      PacedPacketInfo(1, kProbeMinProbes, kProbeMinBytes)));

  packet_router.RemoveSendRtpModule(&rtp_1);

  // rtp_1 has been removed, try sending a packet on that ssrc and make sure
  // it is dropped as expected by not expecting any calls to rtp_1.
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2, SSRC()).Times(1).WillOnce(Return(kSsrc2));
  EXPECT_CALL(rtp_2, TimeToSendPacket(_, _, _, _, _)).Times(0);
  EXPECT_TRUE(packet_router.TimeToSendPacket(
      kSsrc1, sequence_number, timestamp, retransmission,
      PacedPacketInfo(PacedPacketInfo::kNotAProbe, kProbeMinBytes,
                      kProbeMinBytes)));

  packet_router.RemoveSendRtpModule(&rtp_2);
}

TEST(PacketRouterTest, TimeToSendPadding) {
  PacketRouter packet_router;

  const uint16_t kSsrc1 = 1234;
  const uint16_t kSsrc2 = 4567;

  NiceMock<MockRtpRtcp> rtp_1;
  EXPECT_CALL(rtp_1, RtxSendStatus()).WillOnce(Return(kRtxOff));
  EXPECT_CALL(rtp_1, SSRC()).WillRepeatedly(Return(kSsrc1));
  NiceMock<MockRtpRtcp> rtp_2;
  // rtp_2 will be prioritized for padding.
  EXPECT_CALL(rtp_2, RtxSendStatus()).WillOnce(Return(kRtxRedundantPayloads));
  EXPECT_CALL(rtp_2, SSRC()).WillRepeatedly(Return(kSsrc2));
  packet_router.AddSendRtpModule(&rtp_1, false);
  packet_router.AddSendRtpModule(&rtp_2, false);

  // Default configuration, sending padding on all modules sending media,
  // ordered by priority (based on rtx mode).
  const size_t requested_padding_bytes = 1000;
  const size_t sent_padding_bytes = 890;
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2, HasBweExtensions()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2,
              TimeToSendPadding(requested_padding_bytes,
                                Field(&PacedPacketInfo::probe_cluster_id, 111)))
      .Times(1)
      .WillOnce(Return(sent_padding_bytes));
  EXPECT_CALL(rtp_1, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_1, HasBweExtensions()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_1,
              TimeToSendPadding(requested_padding_bytes - sent_padding_bytes,
                                Field(&PacedPacketInfo::probe_cluster_id, 111)))
      .Times(1)
      .WillOnce(Return(requested_padding_bytes - sent_padding_bytes));
  EXPECT_EQ(requested_padding_bytes,
            packet_router.TimeToSendPadding(
                requested_padding_bytes,
                PacedPacketInfo(111, kProbeMinBytes, kProbeMinBytes)));

  // Let only the lower priority module be sending and verify the padding
  // request is routed there.
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(false));
  EXPECT_CALL(rtp_2, TimeToSendPadding(requested_padding_bytes, _)).Times(0);
  EXPECT_CALL(rtp_1, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_1, HasBweExtensions()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_1, TimeToSendPadding(_, _))
      .Times(1)
      .WillOnce(Return(sent_padding_bytes));
  EXPECT_EQ(sent_padding_bytes,
            packet_router.TimeToSendPadding(
                requested_padding_bytes,
                PacedPacketInfo(PacedPacketInfo::kNotAProbe, kProbeMinBytes,
                                kProbeMinBytes)));

  // No sending module at all.
  EXPECT_CALL(rtp_1, SendingMedia()).Times(1).WillOnce(Return(false));
  EXPECT_CALL(rtp_1, TimeToSendPadding(requested_padding_bytes, _)).Times(0);
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(false));
  EXPECT_CALL(rtp_2, TimeToSendPadding(_, _)).Times(0);
  EXPECT_EQ(0u,
            packet_router.TimeToSendPadding(
                requested_padding_bytes,
                PacedPacketInfo(PacedPacketInfo::kNotAProbe, kProbeMinBytes,
                                kProbeMinBytes)));

  // Only one module has BWE extensions.
  EXPECT_CALL(rtp_1, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_1, HasBweExtensions()).Times(1).WillOnce(Return(false));
  EXPECT_CALL(rtp_1, TimeToSendPadding(requested_padding_bytes, _)).Times(0);
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2, HasBweExtensions()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2, TimeToSendPadding(requested_padding_bytes, _))
      .Times(1)
      .WillOnce(Return(sent_padding_bytes));
  EXPECT_EQ(sent_padding_bytes,
            packet_router.TimeToSendPadding(
                requested_padding_bytes,
                PacedPacketInfo(PacedPacketInfo::kNotAProbe, kProbeMinBytes,
                                kProbeMinBytes)));

  packet_router.RemoveSendRtpModule(&rtp_1);

  // rtp_1 has been removed, try sending padding and make sure rtp_1 isn't asked
  // to send by not expecting any calls. Instead verify rtp_2 is called.
  EXPECT_CALL(rtp_2, SendingMedia()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2, HasBweExtensions()).Times(1).WillOnce(Return(true));
  EXPECT_CALL(rtp_2, TimeToSendPadding(requested_padding_bytes, _)).Times(1);
  EXPECT_EQ(0u,
            packet_router.TimeToSendPadding(
                requested_padding_bytes,
                PacedPacketInfo(PacedPacketInfo::kNotAProbe, kProbeMinBytes,
                                kProbeMinBytes)));

  packet_router.RemoveSendRtpModule(&rtp_2);
}

TEST(PacketRouterTest, SenderOnlyFunctionsRespectSendingMedia) {
  PacketRouter packet_router;
  NiceMock<MockRtpRtcp> rtp;
  packet_router.AddSendRtpModule(&rtp, false);
  static const uint16_t kSsrc = 1234;
  EXPECT_CALL(rtp, SSRC()).WillRepeatedly(Return(kSsrc));
  EXPECT_CALL(rtp, SendingMedia()).WillRepeatedly(Return(false));

  // Verify that TimeToSendPacket does not end up in a receiver.
  EXPECT_CALL(rtp, TimeToSendPacket(_, _, _, _, _)).Times(0);
  EXPECT_TRUE(packet_router.TimeToSendPacket(
      kSsrc, 1, 1, false, PacedPacketInfo(PacedPacketInfo::kNotAProbe,
                                          kProbeMinBytes, kProbeMinBytes)));
  // Verify that TimeToSendPadding does not end up in a receiver.
  EXPECT_CALL(rtp, TimeToSendPadding(_, _)).Times(0);
  EXPECT_EQ(0u,
            packet_router.TimeToSendPadding(
                200, PacedPacketInfo(PacedPacketInfo::kNotAProbe,
                                     kProbeMinBytes, kProbeMinBytes)));

  packet_router.RemoveSendRtpModule(&rtp);
}

TEST(PacketRouterTest, AllocateSequenceNumbers) {
  PacketRouter packet_router;

  const uint16_t kStartSeq = 0xFFF0;
  const size_t kNumPackets = 32;

  packet_router.SetTransportWideSequenceNumber(kStartSeq - 1);

  for (size_t i = 0; i < kNumPackets; ++i) {
    uint16_t seq = packet_router.AllocateSequenceNumber();
    uint32_t expected_unwrapped_seq = static_cast<uint32_t>(kStartSeq) + i;
    EXPECT_EQ(static_cast<uint16_t>(expected_unwrapped_seq & 0xFFFF), seq);
  }
}

TEST(PacketRouterTest, SendTransportFeedback) {
  PacketRouter packet_router;
  NiceMock<MockRtpRtcp> rtp_1;
  NiceMock<MockRtpRtcp> rtp_2;

  packet_router.AddSendRtpModule(&rtp_1, false);
  packet_router.AddReceiveRtpModule(&rtp_2, false);

  rtcp::TransportFeedback feedback;
  EXPECT_CALL(rtp_1, SendFeedbackPacket(_)).Times(1).WillOnce(Return(true));
  packet_router.SendTransportFeedback(&feedback);
  packet_router.RemoveSendRtpModule(&rtp_1);
  EXPECT_CALL(rtp_2, SendFeedbackPacket(_)).Times(1).WillOnce(Return(true));
  packet_router.SendTransportFeedback(&feedback);
  packet_router.RemoveReceiveRtpModule(&rtp_2);
}

#if RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST(PacketRouterTest, DoubleRegistrationOfSendModuleDisallowed) {
  PacketRouter packet_router;
  NiceMock<MockRtpRtcp> module;

  constexpr bool remb_candidate = false;  // Value irrelevant.
  packet_router.AddSendRtpModule(&module, remb_candidate);
  EXPECT_DEATH(packet_router.AddSendRtpModule(&module, remb_candidate), "");

  // Test tear-down
  packet_router.RemoveSendRtpModule(&module);
}

TEST(PacketRouterTest, DoubleRegistrationOfReceiveModuleDisallowed) {
  PacketRouter packet_router;
  NiceMock<MockRtpRtcp> module;

  constexpr bool remb_candidate = false;  // Value irrelevant.
  packet_router.AddReceiveRtpModule(&module, remb_candidate);
  EXPECT_DEATH(packet_router.AddReceiveRtpModule(&module, remb_candidate), "");

  // Test tear-down
  packet_router.RemoveReceiveRtpModule(&module);
}

TEST(PacketRouterTest, RemovalOfNeverAddedSendModuleDisallowed) {
  PacketRouter packet_router;
  NiceMock<MockRtpRtcp> module;

  EXPECT_DEATH(packet_router.RemoveSendRtpModule(&module), "");
}

TEST(PacketRouterTest, RemovalOfNeverAddedReceiveModuleDisallowed) {
  PacketRouter packet_router;
  NiceMock<MockRtpRtcp> module;

  EXPECT_DEATH(packet_router.RemoveReceiveRtpModule(&module), "");
}
#endif  // RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)

// TODO(eladalon): Remove this test; it should be covered by:
// 1. SendCandidatePreferredOverReceiveCandidate_SendModuleAddedFirst
// 2. SendCandidatePreferredOverReceiveCandidate_ReceiveModuleAddedFirst
// 3. LowerEstimateToSendRemb
// (Not removing in this CL to prove it doesn't break this test.)
TEST(PacketRouterRembTest, PreferSendModuleOverReceiveModule) {
  rtc::ScopedFakeClock clock;
  NiceMock<MockRtpRtcpWithRembTracking> rtp_recv;
  NiceMock<MockRtpRtcpWithRembTracking> rtp_send;
  PacketRouter packet_router;

  packet_router.AddReceiveRtpModule(&rtp_recv, true);
  ASSERT_TRUE(rtp_recv.REMB());

  const uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};

  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Call OnReceiveBitrateChanged twice to get a first estimate.
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  EXPECT_CALL(rtp_recv, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Add a send module, which should be preferred over the receive module.
  packet_router.AddSendRtpModule(&rtp_send, true);
  EXPECT_FALSE(rtp_recv.REMB());
  EXPECT_TRUE(rtp_send.REMB());

  // Lower bitrate to send another REMB packet.
  EXPECT_CALL(rtp_send, SetREMBData(bitrate_estimate - 100, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate - 100);

  packet_router.RemoveSendRtpModule(&rtp_send);
  EXPECT_TRUE(rtp_recv.REMB());
  EXPECT_FALSE(rtp_send.REMB());

  packet_router.RemoveReceiveRtpModule(&rtp_recv);
}

TEST(PacketRouterRembTest, LowerEstimateToSendRemb) {
  rtc::ScopedFakeClock clock;
  NiceMock<MockRtpRtcpWithRembTracking> rtp;
  PacketRouter packet_router;

  packet_router.AddSendRtpModule(&rtp, true);
  EXPECT_TRUE(rtp.REMB());

  uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};

  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Call OnReceiveBitrateChanged twice to get a first estimate.
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  EXPECT_CALL(rtp, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Lower the estimate with more than 3% to trigger a call to SetREMBData right
  // away.
  bitrate_estimate = bitrate_estimate - 100;
  EXPECT_CALL(rtp, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  packet_router.RemoveSendRtpModule(&rtp);
  EXPECT_FALSE(rtp.REMB());
}

TEST(PacketRouterRembTest, VerifyIncreasingAndDecreasing) {
  rtc::ScopedFakeClock clock;
  NiceMock<MockRtpRtcp> rtp;
  PacketRouter packet_router;
  packet_router.AddSendRtpModule(&rtp, true);

  uint32_t bitrate_estimate[] = {456, 789};
  std::vector<uint32_t> ssrcs = {1234, 5678};

  ON_CALL(rtp, REMB()).WillByDefault(Return(true));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate[0]);

  // Call OnReceiveBitrateChanged twice to get a first estimate.
  EXPECT_CALL(rtp, SetREMBData(bitrate_estimate[0], ssrcs)).Times(1);
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate[0]);

  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate[1] + 100);

  // Lower the estimate to trigger a callback.
  EXPECT_CALL(rtp, SetREMBData(bitrate_estimate[1], ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate[1]);

  packet_router.RemoveSendRtpModule(&rtp);
}

TEST(PacketRouterRembTest, NoRembForIncreasedBitrate) {
  rtc::ScopedFakeClock clock;
  NiceMock<MockRtpRtcp> rtp;
  PacketRouter packet_router;
  packet_router.AddSendRtpModule(&rtp, true);

  uint32_t bitrate_estimate = 456;
  std::vector<uint32_t> ssrcs = {1234, 5678};

  ON_CALL(rtp, REMB()).WillByDefault(Return(true));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Call OnReceiveBitrateChanged twice to get a first estimate.
  EXPECT_CALL(rtp, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Increased estimate shouldn't trigger a callback right away.
  EXPECT_CALL(rtp, SetREMBData(_, _)).Times(0);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate + 1);

  // Decreasing the estimate less than 3% shouldn't trigger a new callback.
  EXPECT_CALL(rtp, SetREMBData(_, _)).Times(0);
  int lower_estimate = bitrate_estimate * 98 / 100;
  packet_router.OnReceiveBitrateChanged(ssrcs, lower_estimate);

  packet_router.RemoveSendRtpModule(&rtp);
}

TEST(PacketRouterRembTest, ChangeSendRtpModule) {
  rtc::ScopedFakeClock clock;
  NiceMock<MockRtpRtcp> rtp_send;
  NiceMock<MockRtpRtcp> rtp_recv;
  PacketRouter packet_router;
  packet_router.AddSendRtpModule(&rtp_send, true);
  packet_router.AddReceiveRtpModule(&rtp_recv, true);

  uint32_t bitrate_estimate = 456;
  std::vector<uint32_t> ssrcs = {1234, 5678};

  ON_CALL(rtp_send, REMB()).WillByDefault(Return(true));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Call OnReceiveBitrateChanged twice to get a first estimate.
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  EXPECT_CALL(rtp_send, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Decrease estimate to trigger a REMB.
  bitrate_estimate = bitrate_estimate - 100;
  EXPECT_CALL(rtp_send, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Remove the sending module -> should get remb on the second module.
  packet_router.RemoveSendRtpModule(&rtp_send);

  ON_CALL(rtp_send, REMB()).WillByDefault(Return(false));
  ON_CALL(rtp_recv, REMB()).WillByDefault(Return(true));

  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  bitrate_estimate = bitrate_estimate - 100;
  EXPECT_CALL(rtp_recv, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  packet_router.RemoveReceiveRtpModule(&rtp_recv);
}

TEST(PacketRouterRembTest, OnlyOneRembForRepeatedOnReceiveBitrateChanged) {
  rtc::ScopedFakeClock clock;
  NiceMock<MockRtpRtcp> rtp;
  PacketRouter packet_router;
  packet_router.AddSendRtpModule(&rtp, true);

  uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};

  ON_CALL(rtp, REMB()).WillByDefault(Return(true));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Call OnReceiveBitrateChanged twice to get a first estimate.
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  EXPECT_CALL(rtp, SetREMBData(_, _)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Lower the estimate, should trigger a call to SetREMBData right away.
  bitrate_estimate = bitrate_estimate - 100;
  EXPECT_CALL(rtp, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Call OnReceiveBitrateChanged again, this should not trigger a new callback.
  EXPECT_CALL(rtp, SetREMBData(_, _)).Times(0);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);
  packet_router.RemoveSendRtpModule(&rtp);
}

// Only register receiving modules and make sure we fallback to trigger a REMB
// packet on this one.
TEST(PacketRouterRembTest, NoSendingRtpModule) {
  rtc::ScopedFakeClock clock;
  NiceMock<MockRtpRtcp> rtp;
  PacketRouter packet_router;

  EXPECT_CALL(rtp, SetREMBStatus(true)).Times(1);
  packet_router.AddReceiveRtpModule(&rtp, true);

  uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};

  ON_CALL(rtp, REMB()).WillByDefault(Return(true));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Call OnReceiveBitrateChanged twice to get a first estimate.
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  EXPECT_CALL(rtp, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Lower the estimate to trigger a new packet REMB packet.
  EXPECT_CALL(rtp, SetREMBData(bitrate_estimate - 100, ssrcs)).Times(1);
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate - 100);

  EXPECT_CALL(rtp, SetREMBStatus(false)).Times(1);
  packet_router.RemoveReceiveRtpModule(&rtp);
}

TEST(PacketRouterRembTest, NonCandidateSendRtpModuleNotUsedForRemb) {
  rtc::ScopedFakeClock clock;
  PacketRouter packet_router;
  NiceMock<MockRtpRtcpWithRembTracking> module;

  constexpr bool remb_candidate = false;

  packet_router.AddSendRtpModule(&module, remb_candidate);
  EXPECT_FALSE(module.REMB());

  constexpr uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};
  EXPECT_CALL(module, SetREMBData(_, _)).Times(0);
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Test tear-down
  packet_router.RemoveSendRtpModule(&module);
}

TEST(PacketRouterRembTest, CandidateSendRtpModuleUsedForRemb) {
  rtc::ScopedFakeClock clock;
  PacketRouter packet_router;
  NiceMock<MockRtpRtcpWithRembTracking> module;

  constexpr bool remb_candidate = true;

  packet_router.AddSendRtpModule(&module, remb_candidate);
  EXPECT_TRUE(module.REMB());

  constexpr uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};
  EXPECT_CALL(module, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Test tear-down
  packet_router.RemoveSendRtpModule(&module);
}

TEST(PacketRouterRembTest, NonCandidateReceiveRtpModuleNotUsedForRemb) {
  rtc::ScopedFakeClock clock;
  PacketRouter packet_router;
  NiceMock<MockRtpRtcpWithRembTracking> module;

  constexpr bool remb_candidate = false;

  packet_router.AddReceiveRtpModule(&module, remb_candidate);
  ASSERT_FALSE(module.REMB());

  constexpr uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};
  EXPECT_CALL(module, SetREMBData(_, _)).Times(0);
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Test tear-down
  packet_router.RemoveReceiveRtpModule(&module);
}

TEST(PacketRouterRembTest, CandidateReceiveRtpModuleUsedForRemb) {
  rtc::ScopedFakeClock clock;
  PacketRouter packet_router;
  NiceMock<MockRtpRtcpWithRembTracking> module;

  constexpr bool remb_candidate = true;

  packet_router.AddReceiveRtpModule(&module, remb_candidate);
  EXPECT_TRUE(module.REMB());

  constexpr uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};
  EXPECT_CALL(module, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Test tear-down
  packet_router.RemoveReceiveRtpModule(&module);
}

TEST(PacketRouterRembTest,
     SendCandidatePreferredOverReceiveCandidate_SendModuleAddedFirst) {
  rtc::ScopedFakeClock clock;
  PacketRouter packet_router;
  NiceMock<MockRtpRtcpWithRembTracking> send_module;
  NiceMock<MockRtpRtcpWithRembTracking> receive_module;

  constexpr bool remb_candidate = true;

  // Send module added - activated.
  packet_router.AddSendRtpModule(&send_module, remb_candidate);
  ASSERT_TRUE(send_module.REMB());

  // Receive module added - the send module remains the active one.
  packet_router.AddReceiveRtpModule(&receive_module, remb_candidate);
  EXPECT_TRUE(send_module.REMB());
  EXPECT_FALSE(receive_module.REMB());

  constexpr uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};
  EXPECT_CALL(send_module, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  EXPECT_CALL(receive_module, SetREMBData(_, _)).Times(0);

  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Test tear-down
  packet_router.RemoveReceiveRtpModule(&receive_module);
  packet_router.RemoveSendRtpModule(&send_module);
}

TEST(PacketRouterRembTest,
     SendCandidatePreferredOverReceiveCandidate_ReceiveModuleAddedFirst) {
  rtc::ScopedFakeClock clock;
  PacketRouter packet_router;
  NiceMock<MockRtpRtcpWithRembTracking> send_module;
  NiceMock<MockRtpRtcpWithRembTracking> receive_module;

  constexpr bool remb_candidate = true;

  // Receive module added - activated.
  packet_router.AddReceiveRtpModule(&receive_module, remb_candidate);
  ASSERT_TRUE(receive_module.REMB());

  // Send module added - replaces receive module as active.
  packet_router.AddSendRtpModule(&send_module, remb_candidate);
  EXPECT_FALSE(receive_module.REMB());
  EXPECT_TRUE(send_module.REMB());

  constexpr uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};
  EXPECT_CALL(send_module, SetREMBData(bitrate_estimate, ssrcs)).Times(1);
  EXPECT_CALL(receive_module, SetREMBData(_, _)).Times(0);

  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Test tear-down
  packet_router.RemoveReceiveRtpModule(&receive_module);
  packet_router.RemoveSendRtpModule(&send_module);
}

TEST(PacketRouterRembTest, ReceiveModuleTakesOverWhenLastSendModuleRemoved) {
  rtc::ScopedFakeClock clock;
  PacketRouter packet_router;
  NiceMock<MockRtpRtcpWithRembTracking> send_module;
  NiceMock<MockRtpRtcpWithRembTracking> receive_module;

  constexpr bool remb_candidate = true;

  // Send module active, receive module inactive.
  packet_router.AddSendRtpModule(&send_module, remb_candidate);
  packet_router.AddReceiveRtpModule(&receive_module, remb_candidate);
  ASSERT_TRUE(send_module.REMB());
  ASSERT_FALSE(receive_module.REMB());

  // Send module removed - receive module becomes active.
  packet_router.RemoveSendRtpModule(&send_module);
  EXPECT_FALSE(send_module.REMB());
  EXPECT_TRUE(receive_module.REMB());
  constexpr uint32_t bitrate_estimate = 456;
  const std::vector<uint32_t> ssrcs = {1234};
  EXPECT_CALL(send_module, SetREMBData(_, _)).Times(0);
  EXPECT_CALL(receive_module, SetREMBData(bitrate_estimate, ssrcs)).Times(1);

  clock.AdvanceTime(rtc::TimeDelta::FromMilliseconds(1000));
  packet_router.OnReceiveBitrateChanged(ssrcs, bitrate_estimate);

  // Test tear-down
  packet_router.RemoveReceiveRtpModule(&receive_module);
}

}  // namespace webrtc
