// Regression coverage for model/ControlGating.h, extracted from app/MainComponent
// after a real bug: a track double-click did nothing before ever connecting,
// because the click handler's role check (`role_ != Role::controller`) didn't
// account for solo mode, where role_ defaults to observer and never becomes
// controller. app/ itself is host-checklist-only (05-testing.md), so the fix
// pulled the decision into this pure, Catch2-testable function.

#include <catch2/catch_test_macros.hpp>

#include "model/ControlGating.h"

TEST_CASE("ControlGating: solo playback (never connected) is always enabled", "[controlgating]")
{
    SECTION("not controller, no peer controls")
    {
        REQUIRE(djapp::controlsEnabledLocally(false, false, false));
    }

    SECTION("not controller, a peer somehow reports controlling")
    {
        REQUIRE(djapp::controlsEnabledLocally(false, false, true));
    }
}

TEST_CASE("ControlGating: connected as the controller is always enabled", "[controlgating]")
{
    REQUIRE(djapp::controlsEnabledLocally(true, true, false));
    REQUIRE(djapp::controlsEnabledLocally(true, true, true));
}

TEST_CASE("ControlGating: connected, not controller, no one else controls is enabled", "[controlgating]")
{
    REQUIRE(djapp::controlsEnabledLocally(true, false, false));
}

TEST_CASE("ControlGating: connected, not controller, a peer controls is disabled", "[controlgating]")
{
    REQUIRE_FALSE(djapp::controlsEnabledLocally(true, false, true));
}

TEST_CASE("ControlGating: a deck widget needs both a track and role permission", "[controlgating]")
{
    REQUIRE(djapp::deckControlEnabled(true, true));
    REQUIRE_FALSE(djapp::deckControlEnabled(false, true));
    REQUIRE_FALSE(djapp::deckControlEnabled(true, false));
    REQUIRE_FALSE(djapp::deckControlEnabled(false, false));
}
