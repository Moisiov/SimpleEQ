/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
KarvaEQAudioProcessor::KarvaEQAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

KarvaEQAudioProcessor::~KarvaEQAudioProcessor()
{
}

//==============================================================================
const juce::String KarvaEQAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool KarvaEQAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool KarvaEQAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool KarvaEQAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double KarvaEQAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int KarvaEQAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int KarvaEQAudioProcessor::getCurrentProgram()
{
    return 0;
}

void KarvaEQAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String KarvaEQAudioProcessor::getProgramName (int index)
{
    return {};
}

void KarvaEQAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void KarvaEQAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..

    juce::dsp::ProcessSpec spec;

    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = 1;
    spec.sampleRate = sampleRate;

    leftChain.prepare(spec);
    rightChain.prepare(spec);

    updateFilters();
}

void KarvaEQAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool KarvaEQAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void KarvaEQAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());
    
    updateFilters();

    juce::dsp::AudioBlock<float> block(buffer);
    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);

    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);

    leftChain.process(leftContext);
    rightChain.process(rightContext);
}

//==============================================================================
bool KarvaEQAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* KarvaEQAudioProcessor::createEditor()
{
    return new KarvaEQAudioProcessorEditor(*this);
    // return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void KarvaEQAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.

    juce::MemoryOutputStream mos(destData, true);
    apvts.state.writeToStream(mos);
}

void KarvaEQAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.

    auto tree = juce::ValueTree::readFromData(data, sizeInBytes);
    if (tree.isValid())
    {
        apvts.replaceState(tree);
        updateFilters();
    }
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings settings;

    settings.lowCutFreq = apvts.getRawParameterValue("LowCut freq")->load();
    settings.highCutFreq = apvts.getRawParameterValue("HighCut freq")->load();
    settings.peakFreq = apvts.getRawParameterValue("Peak freq")->load();
    settings.peakGainInDecibels = apvts.getRawParameterValue("Peak gain")->load();
    settings.peakQuality = apvts.getRawParameterValue("Peak quality")->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut slope")->load());
    settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut slope")->load());

    return settings;
}

juce::AudioProcessorValueTreeState::ParameterLayout KarvaEQAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>("LowCut freq", "LowCut freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f), 20.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("HighCut freq", "HighCut freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f), 20000.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak freq", "Peak freq", juce::NormalisableRange<float>(20.f, 20000.f, 1.f, 0.25f), 750.f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak gain", "Peak gain", juce::NormalisableRange<float>(-24.f, 24.f, 0.5f, 1.f), 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>("Peak quality", "Peak quality", juce::NormalisableRange<float>(0.1f, 10.f, 0.05f, 1.f), 1.f));

    juce::StringArray strArr;
    for (int i = 0; i < 4; ++i)
    {
        juce::String str;
        str << (12 + i * 12);
        str << " dB/Oct";
        strArr.add(str);
    }

    layout.add(std::make_unique<juce::AudioParameterChoice>("LowCut slope", "LowCut slope", strArr, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("HighCut slope", "HighCut slope", strArr, 0));

    return layout;
}

Coefficients makePeakFilter(const ChainSettings& chainSettings, double sampleRate)
{
    return juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        sampleRate,
        chainSettings.peakFreq,
        chainSettings.peakQuality,
        juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels)
    );

}

void KarvaEQAudioProcessor::updatePeakFilter(const ChainSettings& chainSettings)
{
    //auto peakCoefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
    //    getSampleRate(),
    //    chainSettings.peakFreq,
    //    chainSettings.peakQuality,
    //    juce::Decibels::decibelsToGain(chainSettings.peakGainInDecibels)
    //);
    auto peakCoefficients = makePeakFilter(chainSettings, getSampleRate());
    updateCoefficients(leftChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
    updateCoefficients(rightChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
}

void updateCoefficients(Coefficients& old, const Coefficients& replacements)
{
    *old = *replacements;
}

void KarvaEQAudioProcessor::updateLowCutFilters(const ChainSettings& chainSettings)
{
    auto lowCutCoefficients = makeLowCutFilter(chainSettings, getSampleRate());

    auto& leftLowCut = leftChain.get<ChainPositions::LowCut>();
    auto& rightLowCut = rightChain.get<ChainPositions::LowCut>();

    updateCutFilter(leftLowCut, lowCutCoefficients, chainSettings.lowCutSlope);
    updateCutFilter(rightLowCut, lowCutCoefficients, chainSettings.lowCutSlope);
}

void KarvaEQAudioProcessor::updateHighCutFilters(const ChainSettings& chainSettings)
{
    auto highCutCoefficients = makeHighCutFilter(chainSettings, getSampleRate());

    auto& leftHighCut = leftChain.get<ChainPositions::HighCut>();
    auto& rightHighCut = rightChain.get<ChainPositions::HighCut>();

    updateCutFilter(leftHighCut, highCutCoefficients, chainSettings.highCutSlope);
    updateCutFilter(rightHighCut, highCutCoefficients, chainSettings.highCutSlope);
}

void KarvaEQAudioProcessor::updateFilters()
{
    auto chainSettings = getChainSettings(apvts);
    updateLowCutFilters(chainSettings);
    updatePeakFilter(chainSettings);
    updateHighCutFilters(chainSettings);
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new KarvaEQAudioProcessor();
}
