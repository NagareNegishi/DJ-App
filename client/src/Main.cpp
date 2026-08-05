#include "app/MainWindow.h"
#include <juce_gui_basics/juce_gui_basics.h>

class DjAppApplication : public juce::JUCEApplication
{
  public:
    const juce::String getApplicationName() override { return "DJ App"; }

    const juce::String getApplicationVersion() override { return "0.1.0"; }

    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        mainWindow = std::make_unique<djapp::MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

  private:
    std::unique_ptr<djapp::MainWindow> mainWindow;
};

START_JUCE_APPLICATION(DjAppApplication)
