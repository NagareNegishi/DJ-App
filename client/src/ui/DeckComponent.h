#pragma once

// ui/ — DeckComponent: one deck's transport, level, and loop controls. Reads
// StateManager for display, writes back through StateManager::applyDelta for
// each user gesture; Loop In usually only records a local pending point until
// Loop Out completes it, but re-arming over an already-active loop also
// applies a synced clear (and cancelling that re-arm a synced restore) — see
// onLoopInClicked/cancelPendingLoopIn. Never touches engine/ directly (the
// composition root owns the one exception, the resume-position provider
// injected here).
// Animates its own playhead between StateManager notifications by
// extrapolating from a locally held anchor — see rebaseAnchorAndRefresh and
// currentDisplayPositionSeconds — since state/PositionClock doesn't exist
// until M7.

#include "model/Types.h"
#include "repository/AudioRepository.h"
#include "state/StateManager.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace djapp
{

class DeckComponent : public juce::Component, private juce::Timer
{
  public:
    DeckComponent(StateManager& stateManager, DeckId deck, AudioRepository& repository,
                  std::function<double()> resumePositionProvider);
    ~DeckComponent() override;

    void setControlsEnabled(bool enabled); // role-based gate — combines with the existing
                                           // "has a track loaded" gate already inside refreshWidgets

    void resized() override;

  private:
    void timerCallback() override; // 30 Hz repaint of the playhead/time readout
    double currentDisplayPositionSeconds() const;
    void rebaseAnchorAndRefresh(const StateDelta& applied, const PlaybackState& newState);
    void refreshWidgets(const PlaybackState& state);
    void updatePositionDisplay();
    void togglePlayPause();
    void onLoopInClicked();
    void onLoopOutClicked();
    void onLoopClearClicked();
    void onRepeatToggled();
    void resetPendingLoopIn();
    void cancelPendingLoopIn();

    StateManager& stateManager_;
    DeckId deck_;
    AudioRepository& repository_;
    std::function<double()> resumePositionProvider_;

    int listenerToken_ = 0;

    // Anchor for currentDisplayPositionSeconds()'s extrapolation: "as of
    // anchorTimestampMs_, the true position was anchorPositionSeconds_,
    // advancing at anchorRate_ while anchorPlaying_." Re-based on every
    // StateManager notification for this deck (see rebaseAnchorAndRefresh),
    // never left stale across a gain/rate/loop-only delta — see the two
    // cases documented there.
    double anchorPositionSeconds_ = 0;
    double anchorTimestampMs_ = 0;
    float anchorRate_ = 1.0f;
    bool anchorPlaying_ = false;
    std::optional<LoopPoints> anchorLoop_;
    bool anchorRepeat_ = false; // M9: whole-track wrap for currentDisplayPositionSeconds()
                               // when there's no active loop region

    std::optional<double> pendingLoopInSeconds_; // the raw capture itself is never sent over the
                                                 // wire, but arming or cancelling can still trigger a synced loop
                                                 // clear/restore via stashedLoopOnArm_ below
    std::optional<LoopPoints>
        stashedLoopOnArm_; // the loop that was active when
                           // the current arm gesture began, if any — restored on Cancel or on a
                           // rejected Loop Out; discarded once Loop Out actually commits a new loop

    bool rolePermitsControl_ = true; // starts fully enabled: before ever connecting, this is
                                     // solo local playback (M3-M6 behavior) and must keep working

    juce::Label titleLabel_;
    juce::TextButton playPauseButton_{"Play"};
    juce::Slider positionSlider_;
    juce::Label timeLabel_;
    juce::Slider gainSlider_;
    juce::Slider rateSlider_;
    juce::TextButton loopInButton_{"Loop In"};
    juce::TextButton loopOutButton_{"Loop Out"};
    juce::TextButton loopClearButton_{"Clear Loop"};
    juce::DrawableButton repeatButton_{"Repeat", juce::DrawableButton::ImageFitted};
};

} // namespace djapp
