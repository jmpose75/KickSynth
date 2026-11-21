#include "MainComponent.h"

MainComponent::MainComponent()
{
    // ---- Controles del Bombo y Detune ----
    setupSlider(volumeSlider, volumeLabel, 0.0, 1.0, volume, "");
    setupSlider(punchSlider, punchLabel, 0.0, 500.0, punchHz, " Hz");
    setupSlider(pitchDecaySlider, pitchDecayLabel, 0.005, 0.300, pitchDecay, " s");
    setupSlider(detuneSlider, detuneLabel, -50.0, 50.0, detuneCents, " ct");
    setupSlider(detuneMixSlider, detuneMixLabel, 0.0, 1.0, detuneMix, "");

    // ---- Envolvente de Amplitud ----
    setupSlider(attackSlider, attackLabel, 0.001, 0.5, attack, " s");
    setupSlider(decaySlider, decayLabel, 0.02, 1.5, decay, " s");
    setupSlider(sustainSlider, sustainLabel, 0.0, 1.0, sustain, "");
    setupSlider(releaseSlider, releaseLabel, 0.01, 1.5, release, " s");

    // ==== MIDI ====
    addAndMakeVisible(keyboardComponent);
    addAndMakeVisible(midiLabel);
    midiLabel.setText("Toca notas graves (C1 - C2) con el mouse o teclas Z, X, C...", juce::dontSendNotification);

    setSize(700, 580);
    setAudioChannels(0, 2);

    for (auto& input : juce::MidiInput::getAvailableDevices())
        deviceManager.setMidiInputDeviceEnabled(input.identifier, true);

    deviceManager.addMidiInputDeviceCallback("", &midiCollector);

    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    deviceManager.removeMidiInputDeviceCallback("", &midiCollector);
    shutdownAudio();
}

void MainComponent::setupSlider(juce::Slider& slider, juce::Label& label, double min, double max,
    std::atomic<float>& parameter, const juce::String& suffix)
{
    slider.setRange(min, max, 0.001);
    slider.setValue(parameter);
    slider.setTextValueSuffix(suffix);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    slider.onValueChange = [&slider, &parameter] { parameter = (float)slider.getValue(); };
    addAndMakeVisible(slider);
    addAndMakeVisible(label);
}

//==============================================================================
// Audio

void MainComponent::prepareToPlay(int, double sampleRate)
{
    sr = sampleRate;
    phase = 0.0f;
    phase2 = 0.0f;
    stage = Idle;
    envelope = 0.0f;
    pitchEnv = 0.0f;
    midiCollector.reset(sampleRate);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto* buffer = bufferToFill.buffer;

    juce::MidiBuffer midi;
    midiCollector.removeNextBlockOfMessages(midi, bufferToFill.numSamples);
    keyboardState.processNextMidiBuffer(midi, bufferToFill.startSample, bufferToFill.numSamples, true);

    for (const auto metadata : midi)
        handleMidiMessage(metadata.getMessage());

    renderSamples(*buffer, bufferToFill.startSample, bufferToFill.numSamples);
}

void MainComponent::renderSamples(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    float vol = volume;
    float baseFreq = frequency;
    float punch = punchHz;
    float pDecaySec = pitchDecay;
    float cents = detuneCents;
    float mix2 = detuneMix;

    // Factor multiplicador de frecuencia para desafinar en cents (1200 cents = 1 octava)
    float detuneFactor = std::pow(2.0f, cents / 1200.0f);

    // Coeficiente de caída exponencial por muestra para la envolvente de tono
    float pitchDecayCoeff = std::exp(-1.0f / (pDecaySec * (float)sr));

    // Normalización suave para que al sumar ambos osciladores no sature
    float normFactor = 1.0f / (1.0f + mix2);

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Calculamos las frecuencias instantáneas de ambos osciladores
        float currentFreq1 = baseFreq + (punch * pitchEnv);
        float currentFreq2 = currentFreq1 * detuneFactor;

        // 2. Generamos ambos osciladores sinusoidales y los mezclamos
        float osc1 = std::sin(juce::MathConstants<float>::twoPi * phase);
        float osc2 = std::sin(juce::MathConstants<float>::twoPi * phase2);
        float combinedOsc = (osc1 + osc2 * mix2) * normFactor;

        // 3. Aplicamos la envolvente de volumen y la velocity
        float sample = combinedOsc * nextEnvelopeValue() * velocityGain;

        int index = startSample + i;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, index, sample * vol);

        // 4. Avanzamos ambas fases y la envolvente de tono
        phase += currentFreq1 / (float)sr;
        if (phase >= 1.0f)
            phase -= 1.0f;

        phase2 += currentFreq2 / (float)sr;
        if (phase2 >= 1.0f)
            phase2 -= 1.0f;

        pitchEnv *= pitchDecayCoeff;
    }
}

void MainComponent::releaseResources()
{
}

//==============================================================================
// ==== MIDI ====

void MainComponent::handleMidiMessage(const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        currentNote = message.getNoteNumber();

        frequency = 440.0f * std::pow(2.0f, (currentNote - 81) / 12.0f);
        velocityGain = message.getVelocity() / 127.0f;

        // Reiniciamos ambas fases en 0 para que el ataque sea limpio y potente
        phase = 0.0f;
        phase2 = 0.0f;

        pitchEnv = 1.0f;
        stage = Attack;
    }
    else if (message.isNoteOff())
    {
        if (message.getNoteNumber() == currentNote)
        {
            stage = Release;
            releaseStep = envelope / (release * (float)sr);
            currentNote = -1;
        }
    }
    else
    {
        return;
    }

    lastWasNoteOn = message.isNoteOn();
    lastNote = message.getNoteNumber();
    lastVelocity = message.getVelocity();
}

//==============================================================================
// ---- Envolvente ADSR ----

float MainComponent::nextEnvelopeValue()
{
    if (stage == Attack)
    {
        envelope += 1.0f / (attack * (float)sr);
        if (envelope >= 1.0f)
        {
            envelope = 1.0f;
            stage = Decay;
        }
    }
    else if (stage == Decay)
    {
        envelope -= (1.0f - sustain) / (decay * (float)sr);
        if (envelope <= sustain)
        {
            envelope = sustain;
            stage = Sustain;
        }
    }
    else if (stage == Sustain)
    {
        envelope = sustain;
    }
    else if (stage == Release)
    {
        envelope -= releaseStep;
        if (envelope <= 0.0f)
        {
            envelope = 0.0f;
            stage = Idle;
        }
    }
    else
    {
        envelope = 0.0f;
    }

    return envelope;
}

//==============================================================================
// Interfaz gráfica

void MainComponent::timerCallback()
{
    int note = lastNote;
    if (note < 0)
        return;

    midiLabel.setText(juce::String(lastWasNoteOn ? "Note on " : "Note off")
        + "   nota " + juce::String(note)
        + " (" + juce::MidiMessage::getMidiNoteName(note, true, true, 3) + ")"
        + "   velocity " + juce::String(lastVelocity.load()),
        juce::dontSendNotification);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(20);

    juce::Slider* sliders[] = {
        &volumeSlider, &punchSlider, &pitchDecaySlider, &detuneSlider, &detuneMixSlider,
        &attackSlider, &decaySlider, &sustainSlider, &releaseSlider
    };
    juce::Label* labels[] = {
        &volumeLabel, &punchLabel, &pitchDecayLabel, &detuneLabel, &detuneMixLabel,
        &attackLabel, &decayLabel, &sustainLabel, &releaseLabel
    };

    for (int i = 0; i < 9; ++i)
    {
        auto row = area.removeFromTop(26);
        labels[i]->setBounds(row.removeFromLeft(115));
        sliders[i]->setBounds(row);
        area.removeFromTop(5);
    }

    area.removeFromTop(10);

    // ==== MIDI ====
    midiLabel.setBounds(area.removeFromTop(25));
    area.removeFromTop(5);
    keyboardComponent.setBounds(area.removeFromTop(100));
}
