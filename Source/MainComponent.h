#pragma once

#include <JuceHeader.h>

class MainComponent : public juce::AudioAppComponent,
    private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // Audio
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    // Interfaz gráfica
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    // ---- Oscilador y parámetros del Bombo (Kick) ----
    std::atomic<float> volume{ 0.5f };
    std::atomic<float> punchHz{ 180.0f }; // Salto de frecuencia inicial del golpe
    std::atomic<float> pitchDecay{ 0.045f }; // Tiempo de caída del tono (en segundos)

    double sr = 44100.0;
    float phase = 0.0f;
    float frequency = 55.0f; // Nota base grave (La1 = 55 Hz por defecto)
    float pitchEnv = 0.0f;   // Envolvente de tono (arranca en 1.0 al golpear)

    juce::Slider volumeSlider, punchSlider, pitchDecaySlider;
    juce::Label  volumeLabel{ {}, "Volumen" };
    juce::Label  punchLabel{ {}, "Punch (Hz)" };
    juce::Label  pitchDecayLabel{ {}, "Pitch Decay" };

    // ---- Envolvente ADSR de amplitud (configurada percusiva por defecto) ----
    std::atomic<float> attack{ 0.001f };
    std::atomic<float> decay{ 0.28f };
    std::atomic<float> sustain{ 0.0f };
    std::atomic<float> release{ 0.12f };

    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle;
    float envelope = 0.0f;
    float releaseStep = 0.0f;

    float nextEnvelopeValue();

    juce::Slider attackSlider, decaySlider, sustainSlider, releaseSlider;
    juce::Label  attackLabel{ {}, "Attack" };
    juce::Label  decayLabel{ {}, "Decay" };
    juce::Label  sustainLabel{ {}, "Sustain" };
    juce::Label  releaseLabel{ {}, "Release" };

    void setupSlider(juce::Slider& slider, juce::Label& label, double min, double max,
        std::atomic<float>& parameter, const juce::String& suffix);

    // ==== MIDI ====
    juce::MidiKeyboardState     keyboardState;
    juce::MidiKeyboardComponent keyboardComponent{ keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::MidiMessageCollector midiCollector;

    int   currentNote = -1;
    float velocityGain = 0.0f;

    void handleMidiMessage(const juce::MidiMessage& message);
    void renderSamples(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    std::atomic<bool> lastWasNoteOn{ false };
    std::atomic<int>  lastNote{ -1 };
    std::atomic<int>  lastVelocity{ 0 };
    juce::Label midiLabel;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
