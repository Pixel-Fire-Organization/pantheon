#pragma once

#include <cstdint>

/// Achievement recording, supplied only by platforms that have achievements.
/// Platform::GetAchievements() returns null on those that do not.
class AchievementContract
{
public:
    virtual ~AchievementContract() = default;

    AchievementContract(const AchievementContract&) = delete;
    AchievementContract(AchievementContract&&) = delete;
    AchievementContract& operator=(const AchievementContract&) = delete;
    AchievementContract& operator=(AchievementContract&&) = delete;

    /// Bind to the title achievement set.
    /// @param commId Identifies the set to the platform service.
    /// @return False when nothing can be recorded; not an engine failure.
    virtual bool Init(const char* commId) = 0;
    virtual void Shutdown() = 0;

    /// Record an achievement as earned. Idempotent.
    /// @param id Achievement identifier.
    /// @return Whether the platform accepted it.
    virtual bool Unlock(uint32_t id) = 0;

    /// @param id Achievement identifier.
    /// @return False when not unlocked, unknown, or unavailable.
    virtual bool IsUnlocked(uint32_t id) const = 0;

    /// @return Achievements this title declares, whether or not any can be
    ///         recorded. Availability is a separate question, and answering
    ///         both with one number makes a title that declares none
    ///         indistinguishable from a platform that refused.
    virtual uint32_t GetCount() const = 0;

    /// @return How many the platform itself holds, which can be fewer than the
    ///         title declares when it never took delivery of them, or -1 when
    ///         the platform would not say. Not answering is not an answer of
    ///         zero, and the two need telling apart.
    virtual int32_t GetConsoleCount() const = 0;

    /// Advance whatever binding could not finish before frames were running.
    ///
    /// A platform may require the title to install its achievement set before
    /// anything can be read or written, through a system dialog that only
    /// advances while the title presents frames. That cannot happen during
    /// start, so start opens it and this finishes it. Called once per frame
    /// until it answers false, and never again after that.
    /// @return True while binding is still settling.
    virtual bool PumpStartup() { return false; }

    /// @return Whether anything can actually be recorded right now.
    virtual bool IsAvailable() const = 0;

    /// @return Why nothing can be recorded, in terms a player or a developer
    ///         can act on, or null when it can. The two causes need opposite
    ///         fixes -- one is a packaging mistake, the other is the console
    ///         refusing -- and look identical without this.
    virtual const char* GetUnavailableReason() const = 0;

protected:
    AchievementContract() = default;
};
