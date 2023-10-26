/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <utils/Mutex.h>
#include <condition_variable>
#include <list>
#include <map>
#include <optional>
#include <queue>
#include <thread>

#include "../libdevice/ExynosDisplay.h"
#include "RingBuffer.h"
#include "VariableRefreshRateInterface.h"

namespace android::hardware::graphics::composer {

constexpr uint64_t kMillisecondToNanoSecond = 1000000;

class VariableRefreshRateController : public VsyncListener, public PresentListener {
public:
    ~VariableRefreshRateController() { stopThread(); };

    auto static CreateInstance(ExynosDisplay* display)
            -> std::shared_ptr<VariableRefreshRateController>;

    int notifyExpectedPresent(int64_t timestamp, int32_t frameIntervalNs);

    // Clear historical record data.
    void reset();

    // After setting the active Vrr configuration, we will automatically transition into the
    // rendering state and post the timeout event.
    void setActiveVrrConfiguration(hwc2_config_t config);

    void setEnable(bool isEnabled);

    void setVrrConfigurations(std::unordered_map<hwc2_config_t, VrrConfig_t> configs);

private:
    static constexpr int kDefaultRingBufferCapacity = 128;
    static constexpr int64_t kDefaultWakeUpTimeInPowerSaving =
            100 * (std::nano::den / std::milli::den); // 10 Hz = 100 ms

    enum class VrrControllerState {
        kDisable = 0,
        kRendering,
        kHibernate,
    };

    typedef struct PresentEvent {
        hwc2_config_t config;
        int64_t time;
        int duration;
    } PresentEvent;

    typedef struct VsyncEvent {
        hwc2_config_t config;
        int64_t time;
    } VsyncEvent;

    typedef struct VrrRecord {
        static constexpr int kDefaultRingBufferCapacity = 128;

        void clear() {
            mNextExpectedPresentTime = std::nullopt;
            mPendingCurrentPresentTime = std::nullopt;
            mPresentHistory.clear();
            mVsyncHistory.clear();
        }

        std::optional<PresentEvent> mNextExpectedPresentTime = std::nullopt;
        std::optional<PresentEvent> mPendingCurrentPresentTime = std::nullopt;
        ;
        typedef RingBuffer<PresentEvent, kDefaultRingBufferCapacity> PresentTimeRecord;
        typedef RingBuffer<VsyncEvent, kDefaultRingBufferCapacity> VsyncRecord;
        PresentTimeRecord mPresentHistory;
        VsyncRecord mVsyncHistory;
    } VrrRecord;

    enum VrrControllerEventType {
        kRenderingTimeout = 0,
        kHibernateTimeout,
        kNotifyExpectedPresentConfig,
        // Sensors, outer events...
    };

    struct VrrControllerEvent {
        bool operator<(const VrrControllerEvent& b) const { return mWhenNs < b.mWhenNs; }
        std::string getName() const {
            switch (mEventType) {
                case kRenderingTimeout:
                    return "RenderingTimeout";
                case kHibernateTimeout:
                    return "kHibernateTimeout";
                case kNotifyExpectedPresentConfig:
                    return "NotifyExpectedPresentConfig";
                default:
                    return "Unknown";
            }
        }

        std::string toString() const {
            std::ostringstream os;
            os << "Vrr event: [";
            os << "type = " << getName() << ", ";
            os << "when = " << mWhenNs << "ns]";
            return os.str();
        }
        int64_t mDisplay;
        VrrControllerEventType mEventType;
        int64_t mWhenNs;
    };

    VariableRefreshRateController(ExynosDisplay* display);

    // Implement interface PresentListener.
    virtual void onPresent() override;
    virtual void setExpectedPresentTime(int64_t timestampNanos, int frameIntervalNs) override;

    // Implement interface VsyncListener.
    virtual void onVsync(int64_t timestamp, int32_t vsyncPeriodNanos) override;

    void dropEventLocked();
    void dropEventLocked(VrrControllerEventType event_type);

    std::string dumpEventQueueLocked();

    int64_t getNextEventTimeLocked() const;

    std::string getStateName(VrrControllerState state) const;

    // Functions responsible for state machine transitions.
    void handleCadenceChange();
    void handleResume();
    void handleHibernate();
    void handleStayHibernate();

    void postEvent(VrrControllerEventType type, int64_t when);

    void stopThread();

    // The core function of the VRR controller thread.
    void threadBody();

    ExynosDisplay* __unused mDisplay;

    // The subsequent variables must be guarded by mMutex when accessed.
    std::priority_queue<VrrControllerEvent> mEventQueue;
    VrrRecord mRecord;
    VrrControllerState mState;
    hwc2_config_t mVrrActiveConfig;
    std::unordered_map<hwc2_config_t, VrrConfig_t> mVrrConfigs;

    bool mEnabled = false;
    bool mThreadExit = false;

    std::mutex mMutex;
    std::condition_variable mCondition;
};

} // namespace android::hardware::graphics::composer
