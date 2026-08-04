#include "MainWindow.h"

namespace djapp
{

MainWindow::MainWindow(const juce::String& name)
    : DocumentWindow(name, juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                               juce::ResizableWindow::backgroundColourId),
                      DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(), true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
}

void MainWindow::closeButtonPressed()
{
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
}

} // namespace djapp
