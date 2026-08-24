#include "pose_channel.h"

namespace kcd_ht
{
    void PoseChannel::Publish(const HeadPose& pose, bool positionActive, std::uint64_t nowMs)
    {
        const unsigned sequence = m_sequence.load(std::memory_order_relaxed) + 1;
        PoseSnapshot& slot = m_slots[sequence % kSlots];
        slot.pose = pose;
        slot.positionActive = positionActive;
        slot.stampMs = nowMs;
        m_sequence.store(sequence, std::memory_order_release);
        m_valid.store(true, std::memory_order_release);
    }

    void PoseChannel::Invalidate()
    {
        m_valid.store(false, std::memory_order_release);
    }

    bool PoseChannel::TryTake(PoseSnapshot& out, std::uint64_t nowMs) const
    {
        if (!m_valid.load(std::memory_order_acquire)) return false;

        // Read the slot, then check the writer has not lapped the ring while we
        // were copying it. Without the second read the ring is only a very good
        // chance of a whole pose rather than a guarantee, and the one it does
        // not cover - a reader descheduled mid-copy for kSlots frames - is
        // exactly the case a half-written pose would come from.
        const unsigned sequence = m_sequence.load(std::memory_order_acquire);
        out = m_slots[sequence % kSlots];
        if (m_sequence.load(std::memory_order_acquire) - sequence >= kSlots) return false;

        return nowMs - out.stampMs < kFreshnessMs;
    }
}
