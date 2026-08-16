#include "ConnectPanel.h"

namespace djapp
{

ConnectPanel::ConnectPanel()
{
    urlLabel_.setText("URL", juce::dontSendNotification);
    roomLabel_.setText("Room", juce::dontSendNotification);
    nameLabel_.setText("Name", juce::dontSendNotification);

    urlField_.setText("ws://127.0.0.1:8765");
    roomField_.setPasswordCharacter('*'); // the room code is the system's only access
                                           // credential (06-security.md); don't leave it
                                           // visible in cleartext for the whole session

    peerListDisplay_.setMultiLine(true);
    peerListDisplay_.setReadOnly(true);

    connectButton_.onClick = [this] { onConnectButtonClicked(); };
    claimButton_.onClick = [this] { onClaimButtonClicked(); };

    addAndMakeVisible(urlLabel_);
    addAndMakeVisible(roomLabel_);
    addAndMakeVisible(nameLabel_);
    addAndMakeVisible(urlField_);
    addAndMakeVisible(roomField_);
    addAndMakeVisible(nameField_);
    addAndMakeVisible(connectButton_);
    addAndMakeVisible(statusLabel_);
    addAndMakeVisible(claimButton_);
    addAndMakeVisible(peerListDisplay_);

    refreshEnablement();
}

void ConnectPanel::onConnectButtonClicked()
{
    if (!connected_)
    {
        statusLabel_.setText("Connecting...", juce::dontSendNotification);
        ConnectionInfo info{urlField_.getText().trim(), roomField_.getText().trim(), nameField_.getText().trim()};
        if (onConnectRequested)
            onConnectRequested(info);
    }
    else
    {
        if (onDisconnectRequested)
            onDisconnectRequested();
    }
}

void ConnectPanel::onClaimButtonClicked()
{
    if (role_ == Role::controller)
    {
        if (onReleaseRequested)
            onReleaseRequested();
    }
    else
    {
        if (onClaimRequested)
            onClaimRequested();
    }
}

void ConnectPanel::setConnectionStatus(bool connected, const juce::String& statusText)
{
    connected_ = connected;
    statusLabel_.setText(statusText, juce::dontSendNotification);
    statusLabel_.setColour(juce::Label::backgroundColourId,
                           connected ? juce::Colours::green : juce::Colours::darkgrey);
    refreshEnablement();
}

void ConnectPanel::setRole(Role role)
{
    role_ = role;
    refreshEnablement();
}

void ConnectPanel::setPeers(std::vector<PeerInfo> peers)
{
    peers_ = std::move(peers);
    refreshPeerListDisplay();
}

void ConnectPanel::refreshEnablement()
{
    connectButton_.setButtonText(connected_ ? "Disconnect" : "Connect");
    urlField_.setEnabled(!connected_);
    roomField_.setEnabled(!connected_);
    nameField_.setEnabled(!connected_);
    claimButton_.setEnabled(connected_);
    claimButton_.setButtonText(role_ == Role::controller ? "Release Control" : "Claim Control");
}

void ConnectPanel::refreshPeerListDisplay()
{
    juce::String text;
    for (const auto& peer : peers_)
        text << peer.name << " (" << (peer.role == Role::controller ? "controller" : "observer") << ")\n";
    peerListDisplay_.setText(text, juce::dontSendNotification);
}

void ConnectPanel::resized()
{
    auto bounds = getLocalBounds().reduced(4);

    auto fieldsRow = bounds.removeFromTop(24);
    const int fieldWidth = fieldsRow.getWidth() / 4;
    auto layoutLabelledField = [&](juce::Label& label, juce::TextEditor& field)
    {
        auto slot = fieldsRow.removeFromLeft(fieldWidth);
        label.setBounds(slot.removeFromLeft(40));
        field.setBounds(slot);
    };
    layoutLabelledField(urlLabel_, urlField_);
    layoutLabelledField(roomLabel_, roomField_);
    layoutLabelledField(nameLabel_, nameField_);
    connectButton_.setBounds(fieldsRow);

    bounds.removeFromTop(4);

    auto statusRow = bounds.removeFromTop(24);
    claimButton_.setBounds(statusRow.removeFromRight(120));
    statusLabel_.setBounds(statusRow);

    bounds.removeFromTop(4);
    peerListDisplay_.setBounds(bounds);
}

} // namespace djapp
