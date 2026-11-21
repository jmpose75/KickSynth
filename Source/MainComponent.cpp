#include "MainComponent.h"

MainComponent::MainComponent()
{
    // ---- Controles del Bombo (Kick) ----
    setupSlider(volumeSlider, volumeLabel, 0.0, 1.0, volume, "");
    setupSlider(punchSlider, punchLabel, 0.0, 500.0, punchHz, " Hz");
    setupSlider(pitchDecaySlider, pitchDecayLabel, 0.005, 0.300, pitchDecay, " s");

    // ---- Envolvente de Amplitud ----
    setupSlider(attackSlider, attackLabel, 0.001, 0.5, attack, " s");
    setupSlider(decaySlider, decayLabel, 0.02, 1.5, decay, " s");
    setupSlider(sustainSlider, sustainLabel, 0.0, 1.0, sustain, "");
    setupSlider(releaseSlider, releaseLabel, 0.01, 1.5, release, " s");

    // ==== MIDI ====
    addAndMakeVisible(keyboardComponent);
    addAndMakeVisible(midiLabel);
    midiLabel.setText("Toca notas graves (C1 - C2) con el mouse o teclas Z, X, C...", juce::dontSendNotification);

    setSize(700, 520);
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

    // Coeficiente de caída exponencial por muestra para la envolvente de tono
    float pitchDecayCoeff = std::exp(-1.0f / (pDecaySec * (float)sr));

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Calculamos la frecuencia instantánea del bombo (nota grave + barrido del golpe)
        float currentFreq = baseFreq + (punch * pitchEnv);

        // 2. Generamos la onda sinusoidal
        float sample = std::sin(juce::MathConstants<float>::twoPi * phase);

        // 3. Aplicamos la envolvente de volumen y la velocity
        sample = sample * nextEnvelopeValue() * velocityGain;

        int index = startSample + i;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, index, sample * vol);

        // 4. Avanzamos la fase del oscilador y hacemos caer la envolvente de tono
        phase += currentFreq / (float)sr;
        if (phase >= 1.0f)
            phase -= 1.0f;

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

        // Bajamos una octava la afinación para que el teclado en pantalla (C3) suene grave como un bombo
        frequency = 440.0f * std::pow(2.0f, (currentNote - 81) / 12.0f);
        velocityGain = message.getVelocity() / 127.0f;

        // Reiniciamos la fase en 0 para que cada golpe tenga el mismo ataque limpio sin clics
        phase = 0.0f;

        // Disparamos la envolvente de tono al máximo
        pitchEnv = 1.0f;

        // Disparamos la envolvente de volumen
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
        &volumeSlider, &punchSlider, &pitchDecaySlider,
        &attackSlider, &decaySlider, &sustainSlider, &releaseSlider
    };
    juce::Label* labels[] = {
        &volumeLabel, &punchLabel, &pitchDecayLabel,
        &attackLabel, &decayLabel, &sustainLabel, &releaseLabel
    };

    for (int i = 0; i < 7; ++i)
    {
        auto row = area.removeFromTop(28);
        labels[i]->setBounds(row.removeFromLeft(110));
        sliders[i]->setBounds(row);
        area.removeFromTop(6);
    }

    area.removeFromTop(10);

    // ==== MIDI ====
    midiLabel.setBounds(area.removeFromTop(25));
    area.removeFromTop(5);
    keyboardComponent.setBounds(area.removeFromTop(100));
}
