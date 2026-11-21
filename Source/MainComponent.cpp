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

    // ---- Controles del Delay ----
    setupSlider(delayTimeSlider, delayTimeLabel, 0.05, 1.0, delayTimeSec, " s");
    setupSlider(delayFeedbackSlider, delayFeedbackLabel, 0.0, 0.85, delayFeedback, "");
    setupSlider(delayMixSlider, delayMixLabel, 0.0, 1.0, delayMix, "");

    // ==== MIDI y Foco de Teclado ====
    addAndMakeVisible(keyboardComponent);
    keyboardComponent.setWantsKeyboardFocus(true);
    keyboardComponent.setKeyPressBaseOctave(3); // Arranca en octava grave ideal para bombo
    juce::MessageManager::callAsync([this] { keyboardComponent.grabKeyboardFocus(); });

    addAndMakeVisible(midiLabel);
    midiLabel.setText("Teclas A S D F G H J K para tocar | Z / X para cambiar de octava", juce::dontSendNotification);

    setSize(720, 660);
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

    // Preparamos el buffer circular de Delay para soportar hasta 2 segundos
    int maxDelaySamples = (int)(sampleRate * 2.0);
    delayBuffer.assign(maxDelaySamples, 0.0f);
    delayWritePos = 0;

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

    float dTimeSec = delayTimeSec;
    float dFeedback = delayFeedback;
    float dMix = delayMix;

    float detuneFactor = std::pow(2.0f, cents / 1200.0f);
    float pitchDecayCoeff = std::exp(-1.0f / (pDecaySec * (float)sr));
    float normFactor = 1.0f / (1.0f + mix2);

    // Calculamos cuántas muestras hacia atrás leer en el buffer circular del Delay
    int delayBufferSize = (int)delayBuffer.size();
    int delaySamples = juce::jlimit(1, delayBufferSize - 1, (int)(dTimeSec * (float)sr));

    for (int i = 0; i < numSamples; ++i)
    {
        // 1. Frecuencias instantáneas (Kick + Detune)
        float currentFreq1 = baseFreq + (punch * pitchEnv);
        float currentFreq2 = currentFreq1 * detuneFactor;

        // 2. Osciladores sinusoidales
        float osc1 = std::sin(juce::MathConstants<float>::twoPi * phase);
        float osc2 = std::sin(juce::MathConstants<float>::twoPi * phase2);
        float combinedOsc = (osc1 + osc2 * mix2) * normFactor;

        // 3. Señal seca (Dry) con envolvente de volumen
        float drySample = combinedOsc * nextEnvelopeValue() * velocityGain;

        // 4. Procesamiento de Delay (Buffer Circular)
        float outSample = drySample;
        if (delayBufferSize > 0)
        {
            int readPos = delayWritePos - delaySamples;
            if (readPos < 0)
                readPos += delayBufferSize;

            float delayedSample = delayBuffer[readPos];

            // Escribimos en el buffer: señal nueva + eco realimentado
            delayBuffer[delayWritePos] = drySample + (delayedSample * dFeedback);

            // Avanzamos el puntero circular
            delayWritePos++;
            if (delayWritePos >= delayBufferSize)
                delayWritePos = 0;

            // Mezclamos señal directa con el eco
            outSample = drySample + (delayedSample * dMix);
        }

        // 5. Salida estéreo con volumen maestro
        int index = startSample + i;
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, index, outSample * vol);

        // 6. Actualización de fases y envolvente de tono
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
        &attackSlider, &decaySlider, &sustainSlider, &releaseSlider,
        &delayTimeSlider, &delayFeedbackSlider, &delayMixSlider
    };
    juce::Label* labels[] = {
        &volumeLabel, &punchLabel, &pitchDecayLabel, &detuneLabel, &detuneMixLabel,
        &attackLabel, &decayLabel, &sustainLabel, &releaseLabel,
        &delayTimeLabel, &delayFeedbackLabel, &delayMixLabel
    };

    for (int i = 0; i < 12; ++i)
    {
        auto row = area.removeFromTop(24);
        labels[i]->setBounds(row.removeFromLeft(120));
        sliders[i]->setBounds(row);
        area.removeFromTop(4);
    }

    area.removeFromTop(8);

    // ==== MIDI ====
    midiLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);
    keyboardComponent.setBounds(area.removeFromTop(95));
}
