#pragma once

// sync/ — SyncPublisher: the StateManager listener that forwards local state
// changes to the network. It is the anti-echo-loop gate: only deltas produced
// by this client (DeltaSource::local) while connected and in the controller
// role are forwarded; remote deltas are never re-sent. Threading: constructed
// and used on the message thread only (registers a StateManager::Listener,
// which fires on that thread).

#include "SyncTransport.h"
#include "state/StateManager.h"

namespace djapp
{

enum class Role
{
    controller,
    observer
};

class SyncPublisher
{
  public:
    SyncPublisher(StateManager& stateManager, SyncTransport& transport);
    ~SyncPublisher();

    void setConnected(bool connected);
    void setRole(Role role);

  private:
    StateManager& stateManager_;
    SyncTransport& transport_;
    int listenerToken_ = 0;
    bool connected_ = false;
    Role role_ = Role::observer;
};

} // namespace djapp
