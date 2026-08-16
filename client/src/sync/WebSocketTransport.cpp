#include "WebSocketTransport.h"

#include "model/Ranges.h"
#include "model/Serialization.h"

#include <juce_events/juce_events.h>

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <atomic>
#include <memory>
#include <optional>
#include <utility>

namespace djapp
{

struct WebSocketTransport::Impl
{
    Impl() { ix::initNetSystem(); } // WSAStartup on Windows, no-op elsewhere; safe to call more than once

    ~Impl() { teardown(); }

    void connect(const ConnectionInfo& info, Callbacks callbacks)
    {
        // Well-defined even if already connected: disconnect-then-connect, so a stale
        // alive flag from a previous session can never have its callbacks land here.
        teardown();
        alive_ = std::make_shared<std::atomic<bool>>(true);

        auto helloResult = buildHello(info.displayName, info.roomCode);
        if (!helloResult)
        {
            // Invalid input still goes through marshal(): connect() may be re-entered
            // (and teardown() flip this same alive flag false) before this lambda runs
            // on the message thread, so it needs the same guard as every other callback.
            callbacks_ = std::move(callbacks);
            auto onConnectionChange = callbacks_.onConnectionChange;
            auto reason = helloResult.error;
            marshal(
                [onConnectionChange, reason]()
                {
                    if (onConnectionChange)
                        onConnectionChange(false, reason);
                });
            return;
        }

        callbacks_ = std::move(callbacks);
        helloVar_ = *helloResult;
        lastSeenServerSeq_.reset();
        resetLogGuards();

        ws_ = std::make_unique<ix::WebSocket>();
        ws_->disableAutomaticReconnection(); // M7 task list: reconnect is a UI button, not a library-level loop
        ws_->setUrl(info.url.toStdString());
        ws_->setPingInterval(15);

        ws_->setOnMessageCallback(
            [this](const ix::WebSocketMessagePtr& msg)
            {
                switch (msg->type)
                {
                case ix::WebSocketMessageType::Open:
                    handleOpen();
                    break;
                case ix::WebSocketMessageType::Message:
                    // msg->str's reference does not outlive this callback; handleMessage
                    // finishes using it (parses or copies) before returning.
                    handleMessage(msg->str, msg->binary);
                    break;
                case ix::WebSocketMessageType::Close:
                    handleClose(msg->closeInfo.code);
                    break;
                case ix::WebSocketMessageType::Error:
                    handleError();
                    break;
                case ix::WebSocketMessageType::Ping:
                case ix::WebSocketMessageType::Pong:
                case ix::WebSocketMessageType::Fragment:
                    break; // nothing for the transport to do
                }
            });

        ws_->start();
    }

    void disconnect() { teardown(); }

    void sendDelta(const StateDelta& delta) { sendMessage(buildDelta(delta)); }
    void sendClaimControl() { sendMessage(buildClaimControl()); }
    void sendReleaseControl() { sendMessage(buildReleaseControl()); }
    void sendRequestSnapshot() { sendMessage(buildRequestSnapshot()); }

  private:
    // Same flip-then-teardown on connect(), disconnect(), and destruction: flip the
    // atomic false first (any callback already marshalled onto the message thread will
    // see it when it runs), then synchronously stop and drop the socket (no further
    // network-thread callbacks can fire afterwards), and only then release the alive
    // flag itself — by that point nothing else can be reading it concurrently.
    void teardown()
    {
        if (alive_ != nullptr)
            *alive_ = false;

        if (ws_ != nullptr)
        {
            ws_->stop();
            ws_.reset();
        }

        alive_.reset();
    }

    void resetLogGuards()
    {
        loggedBinaryFrame_.store(false);
        loggedOversizeFrame_.store(false);
        loggedMalformedFrame_.store(false);
        loggedUnrecognizedType_.store(false);
        loggedMalformedDelta_.store(false);
        loggedOversizeOutgoing_.store(false);
    }

    // First line of every lambda marshalled onto the message thread. Captures a copy of
    // the alive flag and the action by value only — never `this`, never a reference into
    // Impl — so a lambda already queued when the transport is destroyed finds *alive ==
    // false and returns without touching anything.
    static void marshal(std::shared_ptr<std::atomic<bool>> alive, std::function<void()> action)
    {
        juce::MessageManager::callAsync(
            [alive, capturedAction = std::move(action)]()
            {
                if (!*alive)
                    return;
                capturedAction();
            });
    }

    void marshal(std::function<void()> action) { marshal(alive_, std::move(action)); }

    void logOnce(std::atomic<bool>& guard, const char* classification, size_t byteCount)
    {
        bool expected = false;
        if (!guard.compare_exchange_strong(expected, true))
            return; // already logged this classification for this connection

        // Fixed classification string and a byte count only — never the raw payload, the
        // server's close/error text, or an outgoing message body (the outgoing hello
        // carries the room code, the system's only access credential).
        juce::Logger::writeToLog("WebSocketTransport: " + juce::String(classification) + " (" +
                                 juce::String(static_cast<int>(byteCount)) + " bytes)");
    }

    void sendMessage(const juce::var& message)
    {
        auto result = serializeForSend(message);
        if (!result)
        {
            // Should be impossible with our own messages; the check catches bugs, it
            // never truncates or sends anyway.
            logOnce(loggedOversizeOutgoing_, "dropped oversize outgoing message", 0);
            return;
        }

        if (ws_ != nullptr)
            ws_->send(result.value.toStdString());
    }

    void handleOpen()
    {
        // Same send path as every other outgoing message — no special-cased send here.
        sendMessage(helloVar_);
    }

    void handleMessage(const std::string& raw, bool binary)
    {
        if (binary)
        {
            // Should never occur from our own server; treated defensively, never crashes.
            logOnce(loggedBinaryFrame_, "dropped binary frame", raw.size());
            return;
        }

        // Checked before parsing: nothing bounds JSON::parse's recursion or parse-time
        // allocation against an arbitrarily large inbound frame otherwise.
        if (raw.size() > 4096)
        {
            logOnce(loggedOversizeFrame_, "dropped oversize frame", raw.size());
            return;
        }

        juce::var parsed;
        const auto text = juce::String::fromUTF8(raw.data(), static_cast<int>(raw.size()));
        const auto parseResult = juce::JSON::parse(text, parsed);

        if (!parseResult.wasOk() || !parsed.isObject())
        {
            logOnce(loggedMalformedFrame_, "dropped malformed frame", raw.size());
            return;
        }

        dispatch(parsed, raw.size());
    }

    void dispatch(const juce::var& parsed, size_t frameBytes)
    {
        const auto type = messageType(parsed);

        if (type == "welcome")
        {
            if (const auto seq = messageServerSeq(parsed))
                lastSeenServerSeq_ = seq;

            // "Connected" means the room session is established, not merely that the
            // socket opened: onConnectionChange(true, ...) is marshalled here, alongside
            // onWelcome, never from the Open handler.
            auto onWelcome = callbacks_.onWelcome;
            auto onConnectionChange = callbacks_.onConnectionChange;
            marshal(
                [onWelcome, onConnectionChange, parsed]()
                {
                    if (onConnectionChange)
                        onConnectionChange(true, "connected");
                    if (onWelcome)
                        onWelcome(parsed);
                });
            return;
        }

        if (type == "delta")
        {
            const auto seq = messageServerSeq(parsed);

            auto parsedDelta = parseDeltaMessage(parsed);
            if (!parsedDelta)
            {
                // A frame this malformed carries no trustworthy serverSeq to compare
                // against, so it must never trigger an outbound requestSnapshot: a
                // malformed-frame flood would otherwise reflect straight back at the
                // server 1:1 and risk tripping its own rate limiter.
                logOnce(loggedMalformedDelta_, "dropped malformed delta", frameBytes);
                return;
            }

            // "Clients track the last seen serverSeq; if a received value is not strictly
            // greater, send requestSnapshot" (02-protocol.md) — a self-healing nudge in
            // addition to, not instead of, normal processing of this delta.
            if (!seq.has_value() || !lastSeenServerSeq_.has_value() || !(*seq > *lastSeenServerSeq_))
                sendRequestSnapshot();

            auto delta = *parsedDelta;
            ranges::clamp(delta); // client clamps, never rejects

            if (seq.has_value())
                lastSeenServerSeq_ = seq;

            auto onRemoteDelta = callbacks_.onRemoteDelta;
            marshal(
                [onRemoteDelta, delta]()
                {
                    if (onRemoteDelta)
                        onRemoteDelta(delta);
                });
            return;
        }

        if (type == "snapshot")
        {
            if (const auto seq = messageServerSeq(parsed))
                lastSeenServerSeq_ = seq;

            auto onServerEvent = callbacks_.onServerEvent;
            marshal(
                [onServerEvent, parsed]()
                {
                    if (onServerEvent)
                        onServerEvent(parsed);
                });
            return;
        }

        if (type == "roleChanged" || type == "peerJoined" || type == "peerLeft" || type == "error")
        {
            auto onServerEvent = callbacks_.onServerEvent;
            marshal(
                [onServerEvent, parsed]()
                {
                    if (onServerEvent)
                        onServerEvent(parsed);
                });
            return;
        }

        logOnce(loggedUnrecognizedType_, "dropped unrecognized message", frameBytes);
    }

    void handleClose(int code)
    {
        // The server's free-text close reason is read (via ix::WebSocketCloseInfo) but
        // discarded here, not forwarded — every documented code already has one fixed,
        // complete meaning, and the server's own accompanying string is not something
        // the client has any protocol reason to trust.
        auto onConnectionChange = callbacks_.onConnectionChange;
        auto reason = describeCloseCode(code);
        marshal(
            [onConnectionChange, reason]()
            {
                if (onConnectionChange)
                    onConnectionChange(false, reason);
            });
    }

    void handleError()
    {
        // Fixed, generic string — never IXWebSocket's own internal error detail.
        auto onConnectionChange = callbacks_.onConnectionChange;
        marshal(
            [onConnectionChange]()
            {
                if (onConnectionChange)
                    onConnectionChange(false, "connection error");
            });
    }

    std::unique_ptr<ix::WebSocket> ws_;
    std::shared_ptr<std::atomic<bool>> alive_;
    Callbacks callbacks_;
    juce::var helloVar_;

    // "Connected" means the room session is established (welcome arrived), not merely
    // that the socket opened; onConnectionChange(true, ...) is only ever marshalled from
    // the "welcome" branch of dispatch(), never from handleOpen().

    // Reset fresh on every connect(); the transport is the only unit that sees every
    // server-to-client message in order, so it owns the drift-resync tracking.
    std::optional<int> lastSeenServerSeq_;

    // One log line per classification per connection: a hostile/broken server sending
    // malformed frames at line rate must not be able to grow the client's log file
    // unbounded.
    std::atomic<bool> loggedBinaryFrame_{false};
    std::atomic<bool> loggedOversizeFrame_{false};
    std::atomic<bool> loggedMalformedFrame_{false};
    std::atomic<bool> loggedUnrecognizedType_{false};
    std::atomic<bool> loggedMalformedDelta_{false};
    std::atomic<bool> loggedOversizeOutgoing_{false};
};

WebSocketTransport::WebSocketTransport() : impl_(std::make_unique<Impl>()) {}

WebSocketTransport::~WebSocketTransport() = default; // Impl's destructor performs the flip-then-teardown

void WebSocketTransport::connect(const ConnectionInfo& info, Callbacks callbacks)
{
    impl_->connect(info, std::move(callbacks));
}

void WebSocketTransport::disconnect()
{
    impl_->disconnect();
}

void WebSocketTransport::sendDelta(const StateDelta& delta)
{
    impl_->sendDelta(delta);
}

void WebSocketTransport::sendClaimControl()
{
    impl_->sendClaimControl();
}

void WebSocketTransport::sendReleaseControl()
{
    impl_->sendReleaseControl();
}

void WebSocketTransport::sendRequestSnapshot()
{
    impl_->sendRequestSnapshot();
}

} // namespace djapp
