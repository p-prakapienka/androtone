#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

// The desktop CMake defines these; the Projucer (Android) build may not.
#ifndef JUCE_APPLICATION_NAME_STRING
    #define JUCE_APPLICATION_NAME_STRING "Natron"
#endif
#ifndef JUCE_APPLICATION_VERSION_STRING
    #define JUCE_APPLICATION_VERSION_STRING "0.0.1"
#endif

class NatronApplication : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override {
        if (auto* main = getMainComponent()) {
            main->saveAutosave();
        }
        mainWindow = nullptr;
    }

    // Android kills backgrounded apps without warning: save whenever we leave the foreground.
    void suspended() override {
        if (auto* main = getMainComponent()) {
            main->saveAutosave();
        }
    }

    bool backButtonPressed() override {
        auto* main = getMainComponent();
        return main != nullptr && main->handleBackButton();
    }

    void systemRequestedQuit() override {
        quit();
    }

private:
    MainComponent* getMainComponent() const {
        return mainWindow != nullptr ? dynamic_cast<MainComponent*>(mainWindow->getContentComponent()) : nullptr;
    }

    class MainWindow : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name) :
            DocumentWindow(name, juce::Colour(0xff1d1f21), DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
#if JUCE_ANDROID || JUCE_IOS
            setFullScreen(true);
            setVisible(true);
            // Immersive: hide the status and navigation bars; a swipe from the edge shows them.
            juce::Desktop::getInstance().setKioskModeComponent(this, false);
#else
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
#endif
        }

        void closeButtonPressed() override {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(NatronApplication)
