#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <phonon.h>

class SteamAudioProcess
{
public:
    const float EARLY_GAIN = 0.5f;   // -6 dB
    //Early reflections should be loud enough to convey space, but never compete with direct sound in energy.
    const float LATE_GAIN = 0.35f;  //  −9 dB

    IPLMaterial wood{};
    IPLMaterial gravel{}; // ghiaia
    IPLMaterial* materials; // Change to a pointer to allow dynamic allocation

    IPLContext ctx;
    IPLContextSettings ctxs{};

    int samplingRate = 44100;
    int frameSize = 1024;

    IPLAudioSettings as{};

    IPLHRTFSettings hrtfSettings{};
    IPLHRTF hrtf;

    // General sound settings
    const int ambiOrder = 1;
    const int ambiCh = (ambiOrder + 1) * (ambiOrder + 1);

    // Sound effect settings
    std::vector<IPLDirectEffectSettings> des{};
    std::vector<IPLDirectEffect> direct{};
    std::vector<IPLBinauralEffectSettings> bs{};
    std::vector<IPLBinauralEffect> bin{};      
    std::vector<IPLReflectionEffectSettings> resEarly{};
    std::vector<IPLReflectionEffect> reflEarly{};
    std::vector<IPLAmbisonicsDecodeEffectSettings> dsEarly{};
    std::vector<IPLAmbisonicsDecodeEffect> decodeEarly{};
    std::vector<IPLReflectionEffectSettings> resLate{};
    std::vector<IPLReflectionEffect> reflLate{};
    std::vector<IPLAmbisonicsDecodeEffectSettings> dsLate{};
    std::vector<IPLAmbisonicsDecodeEffect> decodeLate{};

    std::vector<IPLSimulationInputs> inpsList;

    // Gains
    float directGain = 1.0f;
    float earlyReflectionsGain = EARLY_GAIN;
    float reverbGain = LATE_GAIN;

    // Reflection sound settings
    float irDuration = 1.0f;
    const bool useHybridReverb = false;
    const float earlySeconds = 0.05f;
    int irTotal;
    int irEarly;

    // Scene settings
    const float roomWidth = 10.0f;
    const float roomHeight = 3.0f;
    const float roomDepth = 10.0f;
    IPLSceneSettings ss{};
    IPLScene scene;
    IPLStaticMesh mesh;

    // Simulation settings
    IPLSimulationSettings sims{};
    IPLSimulator sim;
    IPLSimulationSharedInputs shared{};

    // Source settings
    int numSources;
    IPLSourceSettings* sset;
    IPLSource* sources;
    IPLCoordinateSpace3* S;
    //S.right = { 1,0,0 }; S.up = { 0,1,0 }; S.ahead = { 0,0,-1 }; S.origin = { 0.0f,L.origin.y, -3.0f };
    std::vector<std::vector<float>> sourcesPositions;
    std::vector<float*> sourcesData;

    // Listener settings
    IPLCoordinateSpace3 L{};
    // L.right = { 1,0,0 }; L.up = { 0,1,0 }; L.ahead = { 0,0,-1 }; L.origin = { 0.0f,1.7f,0.0f };

    // Audio buffers
    IPLAudioBuffer inMono{}, outDirectBuffer{}, outLateReflectionBuffer{}, outEarlyReflectionBuffer{};
    IPLAudioBuffer outBinauralBuffer{}, outEarlyAmbisonicDecodeBuffer{}, outLateAmbisonicDecodeBuffer{};
    IPLAudioBuffer outMonoReverbBuffer{};
    IPLAudioBuffer directMixBuffer{}, reflectionMixBuffer{}, reverbMixBuffer{};

    IPLAmbisonicsDecodeEffectParams dpar{};
    IPLAmbisonicsDecodeEffectParams dpar2{};

    double* directAudio, * reflectionsAudio, * reverbAudio;

    SteamAudioProcess();
    ~SteamAudioProcess();

    IPLSpeakerLayout speakerLayoutForNumChannels(int numChannels);
    int orderForNumChannels(int numChannels);
    int numChannelsForOrder(int order);
    int numSamplesForDuration(float duration, int samplingRate);
    IPLStaticMesh create_static_scene(float W, float H, float D, IPLScene scene, IPLStaticMesh* mesh);
    float compressSample(float sample, float threshold, float ratio);
    void mixWithCompression(IPLAudioBuffer* input, IPLAudioBuffer* output, bool normalize, bool compress,
        float threshold, float ratio);
    void clearAudioBuffer(IPLAudioBuffer* buffer);
    void copyAudioBuffer(IPLAudioBuffer* dest, const IPLAudioBuffer* src);
    void applyGainToBuffer(IPLAudioBuffer* buffer, int numSamples, float volume);

    void awake();
    int onEnable();
    void update();
    int onDisable();
    void onDestroy();
};