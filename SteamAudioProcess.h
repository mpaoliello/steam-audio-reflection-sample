#pragma once

#include <string>
#include <vector>
#include <cmath>
#include <phonon.h>

class SteamAudioProcess
{
public:
    IPLMaterial wood{};
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

    // Direct sound settings
    IPLDirectEffectSettings des{};
    IPLDirectEffect direct;
    IPLBinauralEffectSettings bs{};
    IPLBinauralEffect bin;
    IPLReflectionEffectSettings res{};
    IPLReflectionEffect refl;
    IPLAmbisonicsDecodeEffectSettings ds{};
	IPLAmbisonicsDecodeEffect decode;
    IPLReflectionEffectSettings res2{};
    IPLReflectionEffect refl2;
	IPLAmbisonicsDecodeEffectSettings ds2{};
	IPLAmbisonicsDecodeEffect decode2;

    // Reflection sound settings
    float irDuration = 0.5f;
    const bool useHybridReverb = false;
    const float earlySeconds = 0.1f;
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

    IPLAmbisonicsDecodeEffectParams dpar{};
    IPLAmbisonicsDecodeEffectParams dpar2{};

    double *directAudio, *reflectionsAudio, *reverbAudio;

    SteamAudioProcess(int numSources = 1);
    ~SteamAudioProcess();

    IPLSpeakerLayout speakerLayoutForNumChannels(int numChannels);
    int orderForNumChannels(int numChannels);
    int numChannelsForOrder(int order);
    int numSamplesForDuration(float duration, int samplingRate);
    IPLStaticMesh create_static_scene(float W, float H, float D, IPLScene scene, IPLStaticMesh* mesh);
	float compressSample(float sample, float threshold,float ratio);
    void mixWithCompression(IPLAudioBuffer* input,  IPLAudioBuffer* output,
		float threshold, float ratio);

    void awake();
    int onEnable();
	void update();
    int onDisable();
	void onDestroy();
};
