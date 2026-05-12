#include "SteamAudioProcess.h"
#include <iostream>

SteamAudioProcess::SteamAudioProcess()
{
	wood.absorption[0] = 0.11f;
	wood.absorption[1] = 0.07f;
	wood.absorption[2] = 0.06f;
	wood.scattering = 0.05f;
	wood.transmission[0] = 0.07f;
	wood.transmission[1] = 0.014f;
	wood.transmission[2] = 0.005f;
	materials = new IPLMaterial[1];
	materials[0] = wood;
}

SteamAudioProcess::~SteamAudioProcess()
{
	delete[] materials;
}

IPLSpeakerLayout SteamAudioProcess::speakerLayoutForNumChannels(int numChannels)
{
	IPLSpeakerLayout speakerLayout;
	speakerLayout.numSpeakers = numChannels;
	speakerLayout.speakers = nullptr;

	if (numChannels == 1)
		speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_MONO;
	else if (numChannels == 2)
		speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_STEREO;
	else if (numChannels == 4)
		speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_QUADRAPHONIC;
	else if (numChannels == 6)
		speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_SURROUND_5_1;
	else if (numChannels == 8)
		speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_SURROUND_7_1;
	else
		speakerLayout.type = IPL_SPEAKERLAYOUTTYPE_CUSTOM;

	return speakerLayout;
}

int SteamAudioProcess::orderForNumChannels(int numChannels)
{
	return static_cast<int>(sqrtf(static_cast<float>(numChannels))) - 1;
}

int SteamAudioProcess::numChannelsForOrder(int order)
{
	return (order + 1) * (order + 1);
}

int SteamAudioProcess::numSamplesForDuration(float duration, int samplingRate)
{
	return static_cast<int>(ceilf(duration * samplingRate));
}

IPLStaticMesh SteamAudioProcess::create_static_scene(float W, float H, float D, IPLScene scene, IPLStaticMesh* mesh)
{
	// Build 6 rectangular faces (two triangles each) for the box, centered at bottom origin:
	const float x0 = -W * 0.5f, x1 = W * 0.5f;
	const float y0 = 0.0f, y1 = H;
	const float z0 = -D * 0.5f, z1 = D * 0.5f;

	// 8 corners
	IPLVector3 v[8] = {
		{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}, // floor 0..3
		{x0,y1,z0},{x1,y1,z0},{x1,y1,z1},{x0,y1,z1}  // ceil  4..7
	};
	// 12 triangles (floor, ceiling, 4 walls)
	IPLTriangle t[12] = {
		{0,1,2},{0,2,3},         // floor
		{4,7,6},{4,6,5},         // ceiling
		{0,4,5},{0,5,1},         // -Z wall
		{1,5,6},{1,6,2},         // +X wall
		{2,6,7},{2,7,3},         // +Z wall
		{3,7,4},{3,4,0}          // -X wall
	};
	IPLint32 matIdx[12]; for (int i = 0;i < 12;++i) matIdx[i] = 0;

	IPLStaticMeshSettings mset{};
	mset.numVertices = 8; mset.numTriangles = 12; mset.numMaterials = 1;
	mset.vertices = v; mset.triangles = t; mset.materialIndices = matIdx; mset.materials = materials;
	if (iplStaticMeshCreate(scene, &mset, mesh) != IPL_STATUS_SUCCESS) { fprintf(stderr, "StaticMeshCreate failed.\n"); return nullptr; }

	if (!mesh) {
		std::cerr << "Failed to create mesh." << std::endl;
		return nullptr;
	}

	std::cout << "Successfully created a mesh of a cube centered at the origin." << std::endl;

	iplStaticMeshAdd(*mesh, scene);
	iplSceneCommit(scene);
}


float SteamAudioProcess::compressSample(float sample, float threshold, float ratio) {
	float absSample = fabs(sample);
	if (absSample <= threshold) {
		return sample; // below threshold, no compression
	}
	float excess = absSample - threshold;
	float compressed = threshold + excess / ratio;
	return (sample < 0 ? -compressed : compressed);
}


//float threshold = 0.8f; // start compressing above 80% of full scale
//float ratio = 4.0f;     // 4:1 compression

void SteamAudioProcess::mixWithCompression(IPLAudioBuffer* input, IPLAudioBuffer* output,
	bool normalize,
	bool compress, float threshold, float ratio) {
	// Step 1: Mix sources
	iplAudioBufferMix(ctx, input, output);

	int numSamples = output->numSamples;
	int numChannels = output->numChannels;

	if (normalize == false)
	{
		// Just clamp values to [-1.0, 1.0]
		for (int channelIdx = 0; channelIdx < numChannels; channelIdx++)
		{
			for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++) {
				output->data[channelIdx][sampleIdx] = fmaxf(fminf(output->data[channelIdx][sampleIdx], 1.0f), -1.0f);
			}
		}
		return; // No normalization requested
	}

	// Step 2: Peak limiting (prevent hard clipping)
	float maxVal = 0.0f;
	for (int channelIdx = 0; channelIdx < numChannels; channelIdx++)
	{
		for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++) {
			float sample = fabs(output->data[channelIdx][sampleIdx]);
			if (sample > maxVal) maxVal = sample;
		}
	}

	if (maxVal > 1.0f) {
		float scale = 1.0f / maxVal;
		for (int channelIdx = 0; channelIdx < numChannels; channelIdx++)
		{
			for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++) {
				output->data[channelIdx][sampleIdx] *= scale;
			}
		}
	}

	if (compress == false) {
		return; // No compression requested
	}

	// Step 3: Apply compression for smoother dynamics
	for (int channelIdx = 0; channelIdx < numChannels; channelIdx++)
	{
		for (int sampleIdx = 0; sampleIdx < numSamples; sampleIdx++)
		{
			output->data[channelIdx][sampleIdx] = compressSample(output->data[channelIdx][sampleIdx], threshold, ratio);
		}
	}
}

void SteamAudioProcess::clearAudioBuffer(IPLAudioBuffer* buffer)
{
	// Iterate over each channel
	for (int i = 0; i < buffer->numChannels; ++i)
	{
		// Zero out the block of samples for the current channel
		memset(buffer->data[i], 0, buffer->numSamples * sizeof(IPLfloat32));
	}
}

void SteamAudioProcess::copyAudioBuffer(IPLAudioBuffer* dest, const IPLAudioBuffer* src) {
	// Iterate over each channel
	for (int i = 0; i < src->numChannels; ++i)
	{
		// Copy the block of samples for the current channel
		memcpy(dest->data[i], src->data[i], src->numSamples * sizeof(IPLfloat32));
	}
}

void SteamAudioProcess::awake()
{

}

int SteamAudioProcess::onEnable()
{
	// Initialize Steam Audio context
	ctxs.version = STEAMAUDIO_VERSION;
	ctxs.simdLevel = IPL_SIMDLEVEL_AVX2;
	ctx = nullptr;
	if (iplContextCreate(&ctxs, &ctx) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplContextCreate failed");
		return 1;
	}

	// Initialize audio settings
	as.samplingRate = samplingRate;
	as.frameSize = frameSize;

	// Create HRTF
	hrtfSettings.type = IPL_HRTFTYPE_DEFAULT;
	hrtfSettings.volume = 1.0f;
	hrtfSettings.normType = IPL_HRTFNORMTYPE_NONE;
	hrtf = nullptr;
	if (iplHRTFCreate(ctx, &as, &hrtfSettings, &hrtf) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplHRTFCreate failed");
		return 1;
	}

	for (int s = 0; s < numSources; s++)
	{
        // Add one element to each vector so index 's' is valid
        des.push_back(IPLDirectEffectSettings{});                 // direct effect settings
        direct.push_back(IPLDirectEffect{});                      // direct effect handle
        bs.push_back(IPLBinauralEffectSettings{});                // binaural settings
        bin.push_back(IPLBinauralEffect{});                       // binaural effect handle
        resEarly.push_back(IPLReflectionEffectSettings{});        // early reflection settings
        reflEarly.push_back(IPLReflectionEffect{});               // early reflection effect handle
        dsEarly.push_back(IPLAmbisonicsDecodeEffectSettings{});  // early ambisonics decode settings
        decodeEarly.push_back(IPLAmbisonicsDecodeEffect{});      // early ambisonics decode effect handle
        resLate.push_back(IPLReflectionEffectSettings{});         // late reflection settings
        reflLate.push_back(IPLReflectionEffect{});                // late reflection effect handle
        dsLate.push_back(IPLAmbisonicsDecodeEffectSettings{});   // late ambisonics decode settings
        decodeLate.push_back(IPLAmbisonicsDecodeEffect{});       // late ambisonics decode effect handle

		// Create effects
		des[s].numChannels = 1;
		if (iplDirectEffectCreate(ctx, &as, &des[s], &direct[s]) != IPL_STATUS_SUCCESS)
		{
			std::puts("iplDirectEffectCreate failed");
			return 1;
		}

		bs[s].hrtf = hrtf;
		if (iplBinauralEffectCreate(ctx, &as, &bs[s], &bin[s]) != IPL_STATUS_SUCCESS)
		{
			std::puts("iplBinauralEffectCreate failed");
			return 1;
		}

		if (useHybridReverb)
		{
			resEarly[s].type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
		}
		else
		{
			resEarly[s].type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
		}
		resEarly[s].irSize = numSamplesForDuration(irDuration, samplingRate);
		std::cout << "[ReflectionEffectSettings] irSize = " << resEarly[s].irSize << std::endl;
		resEarly[s].numChannels = ambiCh;
		if (iplReflectionEffectCreate(ctx, &as, &resEarly[s], &reflEarly[s]) != IPL_STATUS_SUCCESS)
		{
			std::puts("iplReflectionEffectCreate failed");
			return 1;
		}

		dsEarly[s].maxOrder = ambiOrder;
		dsEarly[s].hrtf = hrtf;
		IPLSpeakerLayout layout{};
		layout.type = IPL_SPEAKERLAYOUTTYPE_STEREO; // target stereo (+ binaural switch)
		dsEarly[s].speakerLayout = layout;
		if (iplAmbisonicsDecodeEffectCreate(ctx, &as, &dsEarly[s], &decodeEarly[s]) != IPL_STATUS_SUCCESS)
		{
			std::puts("iplAmbisonicsDecodeEffectCreate failed");
			return 1;
		}

		if (useHybridReverb)
		{
			resLate[s].type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
		}
		else
		{
			resLate[s].type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
		}
		resLate[s].irSize = numSamplesForDuration(irDuration, samplingRate);
		std::cout << "[ReflectionEffectSettings] irSize = " << resLate[s].irSize << std::endl;
		resLate[s].numChannels = ambiCh;
		if (iplReflectionEffectCreate(ctx, &as, &resLate[s], &reflLate[s]) != IPL_STATUS_SUCCESS)
		{
			std::puts("iplReflectionEffectCreate failed");
			return 1;
		}

		dsLate[s].maxOrder = ambiOrder;
		dsLate[s].hrtf = hrtf;
		IPLSpeakerLayout layout2{};
		layout2.type = IPL_SPEAKERLAYOUTTYPE_STEREO; // target stereo (+ binaural switch)
		dsLate[s].speakerLayout = layout2;
		if (iplAmbisonicsDecodeEffectCreate(ctx, &as, &dsLate[s], &decodeLate[s]) != IPL_STATUS_SUCCESS)
		{
			std::puts("iplAmbisonicsDecodeEffectCreate failed");
			return 1;
		}
	}

	// Create scene
	ss.type = IPL_SCENETYPE_DEFAULT;
	if (iplSceneCreate(ctx, &ss, &scene) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplSceneCreate failed");
		return 1;
	}
	create_static_scene(roomWidth, roomHeight, roomDepth, scene, &mesh);

	// Set up the simulator
	sims.sceneType = IPL_SCENETYPE_DEFAULT;
	if (useHybridReverb)
	{
		sims.reflectionType = IPL_REFLECTIONEFFECTTYPE_HYBRID;
	}
	sims.flags = IPLSimulationFlags(IPL_SIMULATIONFLAGS_DIRECT | IPL_SIMULATIONFLAGS_REFLECTIONS);
	sims.maxNumOcclusionSamples = 4;
	sims.maxNumRays = 256;
	sims.numDiffuseSamples = 128;
	sims.maxDuration = irDuration;
	sims.maxOrder = ambiOrder;
	sims.maxNumSources = 8;
	sims.numThreads = 2;
	sims.rayBatchSize = 16;
	sims.numVisSamples = 4;
	sims.samplingRate = samplingRate;
	sims.frameSize = frameSize;

	if (iplSimulatorCreate(ctx, &sims, &sim) != IPL_STATUS_SUCCESS)
	{
		std::puts("iplSimulatorCreate failed");
		return 1;
	}
	iplSimulatorSetScene(sim, scene);
	iplSimulatorCommit(sim);

	if (numSources > 0)
	{
		sset = new IPLSourceSettings[numSources]();    // value-initialized
		sources = new IPLSource[numSources]();         // value-initialized
		S = new IPLCoordinateSpace3[numSources]();     // value-initialized
	}
	else
	{
		sset = nullptr;
		sources = nullptr;
		S = nullptr;
	}

	// Initialize source settings
	for (int sourceIndex = 0; sourceIndex < numSources; sourceIndex++)
	{
		sset[sourceIndex].flags = sims.flags;
		sources[sourceIndex] = nullptr;
		if (iplSourceCreate(sim, &sset[sourceIndex], &sources[sourceIndex]) != IPL_STATUS_SUCCESS)
		{
			std::puts("iplSourceCreate failed");
			continue;
		}

		iplSourceAdd(sources[sourceIndex], sim);
		iplSimulatorCommit(sim);
	}

	L.right = { 1,0,0 }; L.up = { 0,1,0 }; L.ahead = { 0,0,-1 };// L.origin = { 0.0f,1.7f,0.0f };

	// Shared simulation inputs
	shared.listener = L;
	shared.numRays = sims.maxNumRays;
	shared.numBounces = 4;
	shared.duration = irDuration;
	shared.order = ambiOrder;
	shared.irradianceMinDistance = 1.0f;

	// Buffer allocations
	iplAudioBufferAllocate(ctx, 1, frameSize, &inMono);
	iplAudioBufferAllocate(ctx, 1, frameSize, &outDirectBuffer); // 1 channel for direct sound
	iplAudioBufferAllocate(ctx, ambiCh, frameSize, &outEarlyReflectionBuffer); // 4 channels for 1st order Ambisonics
	iplAudioBufferAllocate(ctx, ambiCh, frameSize, &outLateReflectionBuffer); // 4 channels for 1st order Ambisonics
	iplAudioBufferAllocate(ctx, 2, frameSize, &outBinauralBuffer);
	iplAudioBufferAllocate(ctx, 2, frameSize, &outEarlyAmbisonicDecodeBuffer);
	iplAudioBufferAllocate(ctx, 2, frameSize, &outLateAmbisonicDecodeBuffer);
	iplAudioBufferAllocate(ctx, 1, frameSize, &outMonoReverbBuffer);
	iplAudioBufferAllocate(ctx, 2, frameSize, &directMixBuffer);
	iplAudioBufferAllocate(ctx, 2, frameSize, &reflectionMixBuffer);
	iplAudioBufferAllocate(ctx, 1, frameSize, &reverbMixBuffer);

	dpar.order = ambiOrder; dpar.hrtf = hrtf; dpar.orientation = L; dpar.binaural = IPL_TRUE;
	dpar2.order = ambiOrder; dpar2.hrtf = hrtf; dpar2.orientation = L; dpar2.binaural = IPL_TRUE;

	irTotal = numSamplesForDuration(irDuration, samplingRate);
	irEarly = numSamplesForDuration(earlySeconds, samplingRate);

	directAudio = (double*)calloc(frameSize * 2, sizeof(double));
	reflectionsAudio = (double*)calloc(frameSize * 2, sizeof(double));
	reverbAudio = (double*)calloc(frameSize, sizeof(double));


	return 0;
}

void SteamAudioProcess::update()
{

	std::vector<IPLSimulationInputs> inpsList;

	// Set source settings for each source
	for (int sourceIndex = 0; sourceIndex < numSources; sourceIndex++)
	{
		IPLSimulationInputs inps{};
		inps.flags = sims.flags;
		inps.directFlags = IPLDirectSimulationFlags(
			IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION
			| IPL_DIRECTSIMULATIONFLAGS_AIRABSORPTION
			//| IPL_DIRECTSIMULATIONFLAGS_OCCLUSION
			//| IPL_DIRECTSIMULATIONFLAGS_TRANSMISSION
		);

		IPLCoordinateSpace3 S;
		S.right = { 1,0,0 };
		S.up = { 0,1,0 };
		S.ahead = { 0,0,-1 };
		S.origin = { sourcesPositions[sourceIndex][0], sourcesPositions[sourceIndex][1], sourcesPositions[sourceIndex][2] };

		// Source settings
		inps.source = S;

		// Distance attenuation settings
		inps.distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;

		// Air absorption settings
		inps.airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;

		// Directivity settings
		inps.directivity.dipoleWeight = 0.0f;
		inps.directivity.dipolePower = 0.0f;

		// Occlusion settings
		inps.occlusionType = IPL_OCCLUSIONTYPE_RAYCAST;
		inps.occlusionRadius = 1.0f;
		inps.numOcclusionSamples = 16;

		// Transmission settings
		inps.numTransmissionRays = 1;

		// Reverb settings
		inps.reverbScale[0] = inps.reverbScale[1] = inps.reverbScale[2] = 1.0f;
		inps.baked = IPL_FALSE;
		inps.pathingProbes = nullptr;
		inps.visRadius = 1.0f;
		inps.visThreshold = 0.1f;
		inps.visRange = 1000.0f;
		inps.pathingOrder = ambiOrder;
		inps.enableValidation = IPL_TRUE;
		inps.findAlternatePaths = IPL_TRUE;


		// Increase for stronger early reflections; costs more CPU.
		inps.hybridReverbTransitionTime = earlySeconds;
		// e.g., 0.20f -> 20% of 100 ms = 20 ms crossfade
		float overlapFrac = 0.25f;
		inps.hybridReverbOverlapPercent = overlapFrac;

		// first only direct
		iplSourceSetInputs(sources[sourceIndex], inps.flags, &inps);

		inpsList.push_back(inps);
	}

	iplSimulatorSetSharedInputs(sim, sims.flags, &shared);
	//iplSimulatorCommit(sim);

	// Run simulation
	iplSimulatorRunDirect(sim);
	iplSimulatorRunReflections(sim);

	// Get simulation outputs for each source
	IPLSimulationOutputs so{};
	for (int s = 0; s < numSources; s++)
	{
		// Fill input mono buffer from source data
		for (int frameIdx = 0;frameIdx < frameSize;++frameIdx)
			// TODO: Is it ok or use memcpy?
			inMono.data[0][frameIdx] = sourcesData[s][frameIdx];

		IPLSource src = sources[s];
		IPLSimulationInputs inps = inpsList[s];
		IPLVector3 dir = { inps.source.origin.x - L.origin.x, inps.source.origin.y - L.origin.y, inps.source.origin.z - L.origin.z };
		iplSourceGetOutputs(src, inps.flags, &so);

		// DIRECT
		IPLDirectEffectParams dp{};
		dp.flags = IPLDirectEffectFlags(IPL_DIRECTEFFECTFLAGS_APPLYDISTANCEATTENUATION | IPL_DIRECTEFFECTFLAGS_APPLYAIRABSORPTION /* | IPL_DIRECTEFFECTFLAGS_APPLYOCCLUSION | IPL_DIRECTEFFECTFLAGS_APPLYTRANSMISSION*/);

		//IPLDistanceAttenuationModel distanceAttenuationModel{};
		//distanceAttenuationModel.type = IPL_DISTANCEATTENUATIONTYPE_DEFAULT;
		//float distanceAttenuation = iplDistanceAttenuationCalculate(ctx, S.origin, L.origin, &distanceAttenuationModel);
		//dp.distanceAttenuation = distanceAttenuation;

		dp.distanceAttenuation = so.direct.distanceAttenuation;

		//IPLAirAbsorptionModel airAbsorptionModel{};
		//airAbsorptionModel.type = IPL_AIRABSORPTIONTYPE_DEFAULT;
		//float airAbsorption[3];
		//iplAirAbsorptionCalculate(ctx, S.origin, L.origin, &airAbsorptionModel, airAbsorption);
		//dp.airAbsorption[0] = airAbsorption[0];
		//dp.airAbsorption[1] = airAbsorption[1];
		//dp.airAbsorption[2] = airAbsorption[2];

		dp.airAbsorption[0] = so.direct.airAbsorption[0];
		dp.airAbsorption[1] = so.direct.airAbsorption[1];
		dp.airAbsorption[2] = so.direct.airAbsorption[2];

		//dp.occlusion = 1;
		//dp.transmission[0] = 1;
		//dp.transmission[1] = 1;
		//dp.transmission[2] = 1;

		dp.occlusion = so.direct.occlusion;
		dp.transmission[0] = so.direct.transmission[0];
		dp.transmission[1] = so.direct.transmission[1];
		dp.transmission[2] = so.direct.transmission[2];

		//dp.directivity = 1.0f; // omnidirectional
		dp.directivity = so.direct.directivity;

		iplDirectEffectApply(direct[s], &dp, &inMono, &outDirectBuffer);

		IPLBinauralEffectParams bp{};
		bp.hrtf = hrtf;
		bp.interpolation = IPL_HRTFINTERPOLATION_BILINEAR;
		bp.spatialBlend = 1.0f;
		bp.direction = dir;
		iplBinauralEffectApply(bin[s], &bp, &outDirectBuffer, &outBinauralBuffer);


		// REFLECTIONS
		float rt60[3] = { so.reflections.reverbTimes[0], so.reflections.reverbTimes[1], so.reflections.reverbTimes[2] };
		float eq3[3] = { so.reflections.eq[0], so.reflections.eq[1], so.reflections.eq[2] };

		// (A) EARLY-ONLY:
		{
			IPLReflectionEffectParams rpEarly{};
			if (useHybridReverb)
			{
				rpEarly.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
				rpEarly.reverbTimes[0] = rpEarly.reverbTimes[1] = rpEarly.reverbTimes[2] = 0.0f;
				rpEarly.delay = so.reflections.delay;
			}
			else
			{
				rpEarly.type = IPL_REFLECTIONEFFECTTYPE_CONVOLUTION;
			}
			rpEarly.ir = so.reflections.ir;
			rpEarly.numChannels = so.reflections.numChannels;
			// no late tail
			// use only early IR
			rpEarly.irSize = irEarly;
			// Apply to mono input -> Ambisonics buffer
			iplReflectionEffectApply(reflEarly[s], &rpEarly, &inMono, &outEarlyReflectionBuffer, nullptr);
			// Ambisonics decode -> stereo binaural
			iplAmbisonicsDecodeEffectApply(decodeEarly[s], &dpar, &outEarlyReflectionBuffer, &outEarlyAmbisonicDecodeBuffer);
		}

		// (B) LATE-ONLY:
		{
			IPLReflectionEffectParams rpLate{};
			if (useHybridReverb)
			{
				rpLate.type = IPL_REFLECTIONEFFECTTYPE_HYBRID;
				rpLate.ir = so.reflections.ir;
				rpLate.delay = so.reflections.delay;
				rpLate.irSize = 0;
				rpLate.eq[0] = eq3[0];
				rpLate.eq[1] = eq3[1];
				rpLate.eq[2] = eq3[2];
			}
			else
			{
				rpLate.type = IPL_REFLECTIONEFFECTTYPE_PARAMETRIC;
			}
			rpLate.numChannels = so.reflections.numChannels;
			rpLate.reverbTimes[0] = rt60[0];
			rpLate.reverbTimes[1] = rt60[1];
			rpLate.reverbTimes[2] = rt60[2];
			// Apply to mono input -> Ambisonics buffer
			iplReflectionEffectApply(reflLate[s], &rpLate, &inMono, &outLateReflectionBuffer, nullptr);
			// Ambisonics decode -> stereo binaural
			iplAmbisonicsDecodeEffectApply(decodeLate[s], &dpar2, &outLateReflectionBuffer, &outLateAmbisonicDecodeBuffer);
			iplAudioBufferDownmix(ctx, &outLateAmbisonicDecodeBuffer, &outMonoReverbBuffer);
		}

		// Mix with compression into final buffers
		if (s == 0)
		{
			copyAudioBuffer(&directMixBuffer, &outBinauralBuffer);
			copyAudioBuffer(&reflectionMixBuffer, &outEarlyAmbisonicDecodeBuffer);
			copyAudioBuffer(&reverbMixBuffer, &outMonoReverbBuffer);
		}
		else
		{
			mixWithCompression(&outBinauralBuffer, &directMixBuffer, false, false, 0.8f, 4.0f);
			mixWithCompression(&outEarlyAmbisonicDecodeBuffer, &reflectionMixBuffer, false, false, 0.8f, 4.0f);
			mixWithCompression(&outMonoReverbBuffer, &reverbMixBuffer, false, false, 0.8f, 4.0f);
		}
	}

	// For loop instead of memcpy due to different data types (float vs double)
	float** srcDirect = directMixBuffer.data;
	float** srcReflections = reflectionMixBuffer.data;
	float** srcReverb = reverbMixBuffer.data;
	for (int channelIdx = 0; channelIdx < 2; channelIdx++)
	{
		for (int sampleIdx = 0; sampleIdx < frameSize; sampleIdx++)
		{
			directAudio[sampleIdx + channelIdx * frameSize] = static_cast<double>(srcDirect[channelIdx][sampleIdx]);
			reflectionsAudio[sampleIdx + channelIdx * frameSize] = static_cast<double>(srcReflections[channelIdx][sampleIdx]);
			if (channelIdx < 1)
				reverbAudio[sampleIdx + channelIdx * frameSize] = static_cast<double>(srcReverb[channelIdx][sampleIdx]);
		}
	}
}

int SteamAudioProcess::onDisable()
{
	// cleanup
	iplAudioBufferFree(ctx, &inMono);
	iplAudioBufferFree(ctx, &outDirectBuffer);
	iplAudioBufferFree(ctx, &outEarlyReflectionBuffer);
	iplAudioBufferFree(ctx, &outLateReflectionBuffer);
	iplAudioBufferFree(ctx, &outBinauralBuffer);
	iplAudioBufferFree(ctx, &outEarlyAmbisonicDecodeBuffer);
	iplAudioBufferFree(ctx, &outLateAmbisonicDecodeBuffer);
	iplAudioBufferFree(ctx, &outMonoReverbBuffer);
	iplAudioBufferFree(ctx, &directMixBuffer);
	iplAudioBufferFree(ctx, &reflectionMixBuffer);
	iplAudioBufferFree(ctx, &reverbMixBuffer);
	for (int i = 0; i < numSources; i++)
	{
		iplReflectionEffectRelease(&reflEarly[i]);
		iplReflectionEffectRelease(&reflLate[i]);
		iplDirectEffectRelease(&direct[i]);
		iplAmbisonicsDecodeEffectRelease(&decodeEarly[i]);
		iplAmbisonicsDecodeEffectRelease(&decodeLate[i]);
		iplBinauralEffectRelease(&bin[i]);
		IPLSource src = sources[i];
		iplSourceRemove(src, sim);
		iplSourceRelease(&src);
	}

	des.clear();
	direct.clear();
	bs.clear();
	bin.clear();
	resEarly.clear();
	reflEarly.clear();
	dsEarly.clear();
	decodeEarly.clear();
	resLate.clear();
	reflLate.clear();
	dsLate.clear();
	decodeLate.clear();

	iplStaticMeshRemove(mesh, scene);
	iplStaticMeshRelease(&mesh);
	iplSceneRelease(&scene);
	iplSimulatorRelease(&sim);
	iplHRTFRelease(&hrtf);
	iplContextRelease(&ctx);

	free(directAudio);
	free(reflectionsAudio);
	free(reverbAudio);

	return 0;
}

void SteamAudioProcess::onDestroy()
{

}
