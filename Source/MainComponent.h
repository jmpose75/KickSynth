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
    // ---- Osciladores y parámetros del Bombo (Kick + Detune) ----
    std::atomic<float> volume{ 0.5f };
    std::atomic<float> punchHz{ 180.0f };
    std::atomic<float> pitchDecay{ 0.045f };
    std::atomic<float> detuneCents{ 15.0f };  // Desafinación en cents (-50 a +50)
    std::atomic<float> detuneMix{ 0.5f };   // Volumen del segundo oscilador (0 a 1)

    double sr = 44100.0;
    float phase = 0.0f;   // Fase Oscilador 1 (principal)
    float phase2 = 0.0f;   // Fase Oscilador 2 (desafinado)
    float frequency = 55.0f;
    float pitchEnv = 0.0f;

    juce::Slider volumeSlider, punchSlider, pitchDecaySlider, detuneSlider, detuneMixSlider;
    juce::Label  volumeLabel{ {}, "Volumen" };
    juce::Label  punchLabel{ {}, "Punch (Hz)" };
    juce::Label  pitchDecayLabel{ {}, "Pitch Decay" };
    juce::Label  detuneLabel{ {}, "Detune (cents)" };
    juce::Label  detuneMixLabel{ {}, "Detune Mix" };

    // ---- Envolvente ADSR de amplitud ----
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
