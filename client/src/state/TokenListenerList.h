#pragma once

// state/ — TokenListenerList: shared listener storage and notification-walk
// machinery for StateManager and CrossfaderState. Both need the same shape
// (token-keyed listener map, monotonic never-reused tokens, a walk that
// re-looks-up the current token fresh on every step so a listener adding or
// removing registrations - including its own - from inside its own callback
// can never invalidate the iteration) and had drifted apart keeping two
// separate copies in sync by comment alone; this extracts the mechanism once.

#include <functional>
#include <map>

namespace djapp
{

template <typename Listener> class TokenListenerList
{
  public:
    // Token starts at 0, increases monotonically, never reused.
    int addListener(Listener listener)
    {
        const int token = nextToken_++;
        listeners_.emplace(token, std::move(listener));
        return token;
    }

    void removeListener(int token) { listeners_.erase(token); }

    bool empty() const { return listeners_.empty(); }

    // Walks registered listeners in registration order, re-looking-up the
    // current token via a fresh map lookup on every step (never holding an
    // iterator across a callback) so mid-walk add/remove can't invalidate
    // iteration. invoke(listenerCopy) is called once per live listener, in
    // whatever way the caller needs (its own argument list). shouldStop() is
    // checked after each listener fires; if it returns true, the walk ends
    // without visiting the remaining listeners - this is what lets a caller
    // like CrossfaderState abandon an outer walk once a re-entrant nested
    // call has already superseded it and covered the rest itself.
    template <typename Invoke, typename ShouldStop> void notify(Invoke&& invoke, ShouldStop&& shouldStop)
    {
        if (listeners_.empty())
            return;

        int token = listeners_.begin()->first;
        for (;;)
        {
            if (auto it = listeners_.find(token); it != listeners_.end())
            {
                const Listener listenerCopy = it->second;
                invoke(listenerCopy);
            }

            if (shouldStop())
                break;

            const auto next = listeners_.upper_bound(token);
            if (next == listeners_.end())
                break;
            token = next->first;
        }
    }

  private:
    std::map<int, Listener> listeners_;
    int nextToken_ = 0;
};

} // namespace djapp
