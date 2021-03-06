/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_VIDEO_VIDEO_SEND_STREAM_H_
#define WEBRTC_VIDEO_VIDEO_SEND_STREAM_H_

#include <map>
#include <memory>
#include <vector>

#include "webrtc/call/bitrate_allocator.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"
#include "webrtc/modules/video_coding/protection_bitrate_calculator.h"
#include "webrtc/rtc_base/criticalsection.h"
#include "webrtc/rtc_base/event.h"
#include "webrtc/rtc_base/task_queue.h"
#include "webrtc/video/encoder_rtcp_feedback.h"
#include "webrtc/video/send_delay_stats.h"
#include "webrtc/video/send_statistics_proxy.h"
#include "webrtc/video/video_stream_encoder.h"
#include "webrtc/video_receive_stream.h"
#include "webrtc/video_send_stream.h"

namespace webrtc {

class CallStats;
class SendSideCongestionController;
class IvfFileWriter;
class ProcessThread;
class RtpRtcp;
class RtpTransportControllerSendInterface;
class RtcEventLog;

namespace internal {

class VideoSendStreamImpl;

// VideoSendStream implements webrtc::VideoSendStream.
// Internally, it delegates all public methods to VideoSendStreamImpl and / or
// VideoStreamEncoder. VideoSendStreamInternal is created and deleted on
// |worker_queue|.
class VideoSendStream : public webrtc::VideoSendStream {
 public:
  VideoSendStream(int num_cpu_cores,
                  ProcessThread* module_process_thread,
                  rtc::TaskQueue* worker_queue,
                  CallStats* call_stats,
                  RtpTransportControllerSendInterface* transport,
                  BitrateAllocator* bitrate_allocator,
                  SendDelayStats* send_delay_stats,
                  RtcEventLog* event_log,
                  VideoSendStream::Config config,
                  VideoEncoderConfig encoder_config,
                  const std::map<uint32_t, RtpState>& suspended_ssrcs,
                  const RtpKeepAliveConfig& keepalive_config);

  ~VideoSendStream() override;

  void SignalNetworkState(NetworkState state);
  bool DeliverRtcp(const uint8_t* packet, size_t length);

  // webrtc::VideoSendStream implementation.
  void Start() override;
  void Stop() override;

  void SetSource(rtc::VideoSourceInterface<webrtc::VideoFrame>* source,
                 const DegradationPreference& degradation_preference) override;

  void ReconfigureVideoEncoder(VideoEncoderConfig) override;
  Stats GetStats() override;

  typedef std::map<uint32_t, RtpState> RtpStateMap;

  // Takes ownership of each file, is responsible for closing them later.
  // Calling this method will close and finalize any current logs.
  // Giving rtc::kInvalidPlatformFileValue in any position disables logging
  // for the corresponding stream.
  // If a frame to be written would make the log too large the write fails and
  // the log is closed and finalized. A |byte_limit| of 0 means no limit.
  void EnableEncodedFrameRecording(const std::vector<rtc::PlatformFile>& files,
                                   size_t byte_limit) override;

  RtpStateMap StopPermanentlyAndGetRtpStates();

  void SetTransportOverhead(size_t transport_overhead_per_packet);

 private:
  class ConstructionTask;
  class DestructAndGetRtpStateTask;

  rtc::ThreadChecker thread_checker_;
  rtc::TaskQueue* const worker_queue_;
  rtc::Event thread_sync_event_;

  SendStatisticsProxy stats_proxy_;
  const VideoSendStream::Config config_;
  const VideoEncoderConfig::ContentType content_type_;
  std::unique_ptr<VideoSendStreamImpl> send_stream_;
  std::unique_ptr<VideoStreamEncoder> video_stream_encoder_;
};

}  // namespace internal
}  // namespace webrtc

#endif  // WEBRTC_VIDEO_VIDEO_SEND_STREAM_H_
