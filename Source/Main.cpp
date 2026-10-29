#include <rb_app/rb_app.h>

class RiffbookApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String& commandLine) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());

        // --snapshot <folder>: render each screen to PNG and quit (headless visual checks).
        auto args = juce::StringArray::fromTokens (commandLine, true);
        if (const int i = args.indexOf ("--snapshot"); i >= 0 && i + 1 < args.size())
        {
            const juce::File folder (args[i + 1].unquoted());
            juce::MessageManager::callAsync ([this, folder]
            {
                if (auto* main = dynamic_cast<rb::MainComponent*> (mainWindow->getContentComponent()))
                    main->writeSnapshots (folder);
                quit();
            });
        }
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colours::black, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new rb::MainComponent(), true);

           #if JUCE_ANDROID || JUCE_IOS
            setFullScreen (true);
           #else
            setResizable (true, true);
            centreWithSize (getWidth(), getHeight());
           #endif

            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (RiffbookApplication)
