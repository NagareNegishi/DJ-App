#include "NullTransport.h"

namespace djapp
{

void NullTransport::connect(const ConnectionInfo&, Callbacks callbacks)
{
    callbacks_ = std::move(callbacks);
}

void NullTransport::disconnect()
{
}

void NullTransport::sendDelta(const StateDelta&)
{
}

void NullTransport::sendClaimControl()
{
}

void NullTransport::sendReleaseControl()
{
}

void NullTransport::sendRequestSnapshot()
{
}

} // namespace djapp
